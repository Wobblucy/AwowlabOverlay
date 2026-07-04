#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include "Actor/ActorMap.h"
#include "structures.h"

// Combat metric type for querying
enum class CombatMetricType {
    DamageDealt,
    HealingDone,
    DamageTaken,       // Damage received by actor (uses damageTakenIndex_)
    // The two below are only meaningful for getSpellBreakdown-style
    // per-actor drill-downs. They should not be used with the ranking
    // queries (getRankedByActor etc) - those still return {} because
    // there's no per-actor combat table for these metrics.
    FriendlyFire,      // This actor's damage records flagged as friendly fire
    HealingReceived    // Healing this actor received, aggregated per source spell
};

// Per-spell aggregated statistics
struct SpellCombatStats {
    uint32_t spell_id = 0;
    int64_t total_amount = 0;
    int64_t effective_amount = 0;
    uint32_t hit_count = 0;
    uint32_t crit_count = 0;
    uint32_t periodic_count = 0;
    int64_t max_hit = 0;
    int64_t min_hit = INT64_MAX;
    uint8_t spell_school = 0;

    // Normal (non-crit) hit breakdown
    int64_t normal_total = 0;
    uint32_t normal_count = 0;
    int64_t normal_min = INT64_MAX;
    int64_t normal_max = 0;

    // Critical hit breakdown
    int64_t crit_total = 0;
    int64_t crit_min = INT64_MAX;
    int64_t crit_max = 0;

    // Per-hit-type bucket for the drill-down Hit Distribution view.
    // Non-zero counts mean at least one record with the corresponding
    // flag was seen. Totals sum the record amount so drill-down can
    // show min/max/avg per bucket.
    struct HitTypeStats {
        uint32_t count = 0;
        int64_t total = 0;
        int64_t min = INT64_MAX;
        int64_t max = 0;
    };
    HitTypeStats glance_hits;
    HitTypeStats resisted_hits;
    HitTypeStats blocked_hits;
    HitTypeStats absorbed_hits;
};

// Per-target aggregated statistics (for tooltip breakdown)
struct TargetCombatStats {
    std::string target_guid;
    int64_t total_amount = 0;
    uint32_t hit_count = 0;
};

// Per-pet aggregated statistics (for tooltip breakdown)
struct PetCombatStats {
    std::string pet_guid;
    int64_t total_amount = 0;
    uint32_t hit_count = 0;
};

// One collapsible group in the spell breakdown holding all abilities a
// given pet TYPE cast. Every spawn of the same summon (all "Lesser Ghoul"
// copies) merges into a single group keyed by NPC id; a real named pet
// (a hunter's pet, npc id 0) gets its own group keyed by its guid. The
// spells inside are aggregated per spell_id across spawns, exactly like
// the owner's own spell_breakdown.
struct PetSpellGroup {
    std::string pet_guid;                    // representative spawn's guid (for name lookup)
    uint32_t npc_id = 0;                      // NPC id shared by the group (0 for real pets)
    int64_t total_amount = 0;
    uint32_t hit_count = 0;
    std::vector<SpellCombatStats> spells;
};

// Per-actor aggregated combat statistics
struct ActorCombatStats {
    std::string actor_guid;
    int64_t total_amount = 0;
    int64_t effective_amount = 0;
    uint32_t hit_count = 0;
    uint32_t crit_count = 0;
    float percent_of_total = 0.0f;
    float amount_per_second = 0.0f;
    std::vector<SpellCombatStats> spell_breakdown;    // Owner's OWN spells only
    std::vector<TargetCombatStats> target_breakdown;  // Damage by target
    std::vector<PetCombatStats> pet_breakdown;        // Damage by pet/guardian
    std::vector<PetSpellGroup> pet_spell_groups;      // Per-pet-type spell groups (from getRankedByActorWithPets)
};

// Query parameters for flexible filtering
struct CombatQuery {
    CombatMetricType metric_type = CombatMetricType::DamageDealt;
    int32_t start_time_ms = 0;
    int32_t end_time_ms = INT32_MAX;
    bool include_pets = true;
};

// Time-bucketed damage data for graphs
struct DamageTimeBucket {
    int32_t timestamp_ms = 0;                               // Start of bucket (negative = before encounter)
    int64_t total_damage = 0;                               // Total damage in bucket
    std::vector<std::pair<uint32_t, int64_t>> by_spell;     // spell_id -> damage (sorted by damage desc)
};

// Individual combat event for death timeline display
// Represents a single damage/healing event affecting a specific target
struct TargetCombatEvent {
    int32_t timestamp_ms = 0;    // Relative timestamp (negative = before encounter)
    uint32_t spell_id = 0;
    std::string source_guid;
    int64_t amount = 0;              // Raw amount
    int64_t effective_amount = 0;    // Amount - overkill/overheal
    bool is_damage = true;           // true = damage taken, false = healing received
    bool is_crit = false;
    bool is_periodic = false;
    uint8_t spell_school = 0;
};

// CombatDatabase - Facade over ActorMap for combat metrics queries
// Provides efficient time-windowed aggregation of damage/healing data
class CombatDatabase {
public:
    CombatDatabase() = default;
    ~CombatDatabase() = default;

    // Load from ActorMap (facade pattern - no data copy, just pointer storage)
    //
    // summonPetToOwner (optional) is the authoritative pet -> owner lineage
    // learned from SPELL_SUMMON, keyed by interned guid id (summoned unit ->
    // summoner). When supplied it lets a player adopt a genuinely-summoned
    // Creature-/Vehicle- pet (a Death Knight's army, a mage's elemental, etc.)
    // that the advanced combat log never tags with an owner. Without it we
    // fall back to scanning record owner_guids, which can only safely adopt a
    // player owner onto a real Pet- guid. Pass nullptr when no summon data is
    // available (hand-built maps in tests, callers that don't track summons).
    void loadFromActorMap(
        const ActorMap* actorMap,
        const std::unordered_map<StringInterner::Id, StringInterner::Id>* summonPetToOwner = nullptr
    );

    // Check if database has data
    bool empty() const { return !actorMap_ || actorMap_->empty(); }

    // Get number of actors with combat data
    size_t actorCount() const { return actorMap_ ? actorMap_->size() : 0; }

    // Get underlying ActorMap for direct access (used by auto-annotation triggers)
    const ActorMap* getActorMap() const { return actorMap_; }

    // ========== Target Weighting ==========
    // Per-mob damage weighting (MobWeightSettings): damage done to a
    // weighted npc counts at that fraction in damage-done queries so
    // padding on unimportant adds stops inflating contribution.
    // Damage taken is never weighted.

    // Recompute the per-target weight cache from MobWeightSettings.
    // Called automatically by loadFromActorMap; call again after the
    // user edits weights so open meters pick the change up. Const so
    // UI code holding a const pointer can trigger the rebuild - the
    // weight cache is derived state, the source of truth lives in
    // MobWeightSettings.
    void refreshTargetWeights() const;

    // True when at least one loaded target carries a weight != 1
    bool hasActiveTargetWeights() const { return !targetWeights_.empty(); }

    // Get time bounds (can be negative for pre-encounter events)
    int32_t getMinTimestamp() const { return min_timestamp_; }
    int32_t getMaxTimestamp() const { return max_timestamp_; }

    // ========== Query Methods ==========

    // Get ranked results for all actors (sorted by total amount descending)
    std::vector<ActorCombatStats> getRankedByActor(
        CombatMetricType type,
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40
    ) const;

    // Get spell breakdown for a specific actor
    std::vector<SpellCombatStats> getSpellBreakdown(
        const std::string& actor_guid,
        CombatMetricType type,
        int32_t start_time_ms,
        int32_t end_time_ms
    ) const;

    // Convenience: Get cumulative stats from encounter start to current time
    std::vector<ActorCombatStats> getCumulative(
        CombatMetricType type,
        int32_t current_time_ms,
        size_t max_results = 40
    ) const;

    // Convenience: Get full encounter stats
    std::vector<ActorCombatStats> getFullEncounter(
        CombatMetricType type,
        size_t max_results = 40
    ) const;

    // Get total for all actors in time window (for percentage calculation)
    int64_t getGrandTotal(
        CombatMetricType type,
        int32_t start_time_ms,
        int32_t end_time_ms
    ) const;

    // ========== Pet/Guardian Attribution ==========

    // Get ranked results with pet/guardian damage merged into owner's total
    // Pets and guardians are identified by having a non-empty owner_guid in their combat records
    // Set includeBreakdowns=true to populate spell_breakdown, target_breakdown, pet_breakdown
    // (expensive - only needed for breakdown panel, not for meter list)
    // If blacklistedSpells is provided, damage from those spells is excluded from actor totals
    // and aggregated into a virtual "Blacklist" actor returned at the end of the results
    std::vector<ActorCombatStats> getRankedByActorWithPets(
        CombatMetricType type,
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40,
        bool includeBreakdowns = false,
        const std::unordered_set<uint32_t>* blacklistedSpells = nullptr
    ) const;

    // Get the owner GUID for a pet/guardian actor (empty if not a pet)
    std::string getOwnerGuid(const std::string& pet_guid) const;

    // Check if an actor is a pet/guardian (has an owner)
    bool isPetOrGuardian(const std::string& actor_guid) const;

    // ========== Damage Taken Queries ==========

    // Get actors ranked by damage taken (sorted descending by amount received)
    std::vector<ActorCombatStats> getRankedByDamageTaken(
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40
    ) const;

    // Get actors ranked by damage taken with pet damage merged into owner's total
    // Set includeBreakdowns=true to populate spell_breakdown with source abilities
    // (expensive - only needed for breakdown panel, not for meter list)
    std::vector<ActorCombatStats> getRankedByDamageTakenWithPets(
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40,
        bool includeBreakdowns = false
    ) const;

    // Get actors ranked by damage dealt TO a specific target
    // Returns source actors sorted by damage done to the target (descending)
    // Used for "Taken By" meter view to see who did damage to a specific enemy
    std::vector<ActorCombatStats> getDamageDoneToTarget(
        const std::string& target_guid,
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40
    ) const;

    // Get actors ranked by damage dealt to friendly targets (players)
    // Returns source actors sorted by friendly fire damage done (descending)
    // Used for "Friendly Fire" meter view
    std::vector<ActorCombatStats> getRankedByFriendlyFire(
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40
    ) const;

    // Per-actor spell breakdown restricted to hits that landed on
    // player targets. Used for the Friendly Fire drill-down so the
    // spell list only shows the abilities the actor actually
    // friendly-fired with, not their full damage rotation.
    std::vector<SpellCombatStats> getFriendlyFireSpellBreakdown(
        const std::string& actor_guid,
        int32_t start_time_ms,
        int32_t end_time_ms
    ) const;

    // Per-actor healing breakdown from the target's perspective:
    // "who healed this actor and with which spells". Walks every
    // source actor's healing_done_table filtered for the target
    // guid. Aggregates per spell_id like the damage/healing per-actor
    // breakdown, so the drill-down renders through the same code.
    std::vector<SpellCombatStats> getHealingReceivedSpellBreakdown(
        const std::string& actor_guid,
        int32_t start_time_ms,
        int32_t end_time_ms
    ) const;

    // Get hostile actors ranked by damage they dealt (enemy DPS view).
    // Source GUIDs that are not Player-* are treated as hostile.
    // Used for "Enemy Damage" meter view.
    std::vector<ActorCombatStats> getRankedByEnemyDamage(
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40
    ) const;

    // Get hostile actors ranked by healing they cast (typically boss
    // self-heals or add heals). Source GUIDs that are not Player-* are
    // treated as hostile. Used for "Enemy Healing" meter view.
    std::vector<ActorCombatStats> getRankedByEnemyHealing(
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40
    ) const;

    // Get actors ranked by overhealing (wasted healing)
    // Returns actors sorted by overhealing amount (descending)
    // Used for "Overhealing" meter view
    std::vector<ActorCombatStats> getRankedByOverhealing(
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40
    ) const;

    // Get actors ranked by healing received
    // Returns actors sorted by healing received (descending)
    // Used for "Healing Taken" meter view
    std::vector<ActorCombatStats> getRankedByHealingTaken(
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40
    ) const;

    // ========== Death Timeline Queries ==========

    // Get individual damage/healing events affecting a specific target
    // Used for death analysis to show what happened before death
    // Returns events sorted by timestamp (most recent first)
    std::vector<TargetCombatEvent> getEventsForTarget(
        const std::string& target_guid,
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_events = 30
    ) const;

    // ========== Time Series Queries ==========

    // Get time-bucketed damage for an actor (including pets) for graphing
    // Returns damage per time bucket, broken down by spell
    // If blacklistedSpells is provided, those spells are excluded from the time series
    std::vector<DamageTimeBucket> getActorDamageTimeSeries(
        const std::string& actor_guid,
        uint32_t bucket_size_ms = 1000,
        const std::unordered_set<uint32_t>* blacklistedSpells = nullptr
    ) const;

    // Get time-bucketed healing for an actor (including pets) for graphing
    // Returns healing per time bucket, broken down by spell
    // If blacklistedSpells is provided, those spells are excluded from the time series
    std::vector<DamageTimeBucket> getActorHealingTimeSeries(
        const std::string& actor_guid,
        uint32_t bucket_size_ms = 1000,
        const std::unordered_set<uint32_t>* blacklistedSpells = nullptr
    ) const;

    // Get time-bucketed damage taken for an actor (personal only, no pets) for graphing
    // Returns damage received per time bucket, broken down by spell (source ability)
    // If blacklistedSpells is provided, those spells are excluded from the time series
    std::vector<DamageTimeBucket> getActorDamageTakenTimeSeries(
        const std::string& actor_guid,
        uint32_t bucket_size_ms = 1000,
        const std::unordered_set<uint32_t>* blacklistedSpells = nullptr
    ) const;

    // ========== Index Accessors ==========

    // Get damage taken index for auto-annotation triggers (indexed by victim GUID)
    const std::unordered_map<uint32_t, std::vector<const CombatRecord*>>& getDamageTakenIndex() const { return damageTakenIndex_; }

    // ========== Debug Methods ==========

    // Dump all raw CombatRecords to CSV files for debugging
    // Creates: combat_records.csv, actor_stats.csv, spell_breakdown.csv
    void dumpToCSV(const std::string& output_dir) const;

#ifndef NDEBUG
    // Debug-only accessors for DebugDatabaseDumper (interned GUID ids)
    const std::unordered_map<StringInterner::Id, StringInterner::Id>& getPetToOwnerMap() const { return petToOwnerMap_; }
    const std::unordered_map<StringInterner::Id, std::vector<StringInterner::Id>>& getOwnerToPetsMap() const { return ownerToPetsMap_; }
    const std::unordered_map<uint32_t, std::vector<const CombatRecord*>>& getHealingReceivedIndex() const { return healingReceivedIndex_; }
#endif

private:
    const ActorMap* actorMap_ = nullptr;
    int32_t min_timestamp_ = 0;
    int32_t max_timestamp_ = 0;

    // Pet/Guardian to Owner mapping (pet guid id -> owner guid id)
    // Built during loadFromActorMap by scanning owner_guid in combat records
    mutable std::unordered_map<StringInterner::Id, StringInterner::Id> petToOwnerMap_;

    // Reverse index: owner guid id -> list of pet guid ids (for efficient pet iteration)
    // Built during loadFromActorMap alongside petToOwnerMap_
    std::unordered_map<StringInterner::Id, std::vector<StringInterner::Id>> ownerToPetsMap_;

    // Cache for time series data (actor_guid -> buckets)
    // Cleared when new data is loaded via loadFromActorMap()
    mutable std::unordered_map<std::string, std::vector<DamageTimeBucket>> timeSeriesCache_;

    // Secondary index: target_guid_id -> pointers to damage records received
    // Built during loadFromActorMap for efficient damage-taken queries
    // Uses interned GUID ids as keys and pointers to ActorMap records (no data copy)
    std::unordered_map<uint32_t, std::vector<const CombatRecord*>> damageTakenIndex_;

    // Secondary index: target_guid_id -> pointers to healing records received
    // Built during loadFromActorMap for efficient healing-received queries (death timeline)
    std::unordered_map<uint32_t, std::vector<const CombatRecord*>> healingReceivedIndex_;

    // Helper to get the correct combat table for an actor based on metric type
    const std::vector<CombatRecord>* getCombatTable(
        const ActorTable& actor_table,
        CombatMetricType type
    ) const;

    // Helper to aggregate stats from a combat table within time range.
    // Takes the interned guid id; the result carries the guid string.
    ActorCombatStats aggregateStats(
        StringInterner::Id guid_id,
        const std::vector<CombatRecord>& records,
        int32_t start_time_ms,
        int32_t end_time_ms
    ) const;

    // Helper to get spell breakdown from records
    std::vector<SpellCombatStats> aggregateSpellBreakdown(
        const std::vector<CombatRecord>& records,
        int32_t start_time_ms,
        int32_t end_time_ms
    ) const;

    // Helper to get target breakdown from records
    std::vector<TargetCombatStats> aggregateTargetBreakdown(
        const std::vector<CombatRecord>& records,
        int32_t start_time_ms,
        int32_t end_time_ms
    ) const;

    // Helper to get pet breakdown for an owner (by interned guid id)
    std::vector<PetCombatStats> aggregatePetBreakdown(
        StringInterner::Id owner_guid_id,
        CombatMetricType type,
        int32_t start_time_ms,
        int32_t end_time_ms
    ) const;

    // Build pet-to-owner mapping. Seeds from the SPELL_SUMMON lineage
    // (authoritative) when available, then fills the rest by scanning
    // record owner_guids for enemy-side summon merges.
    void buildPetToOwnerMap(
        const std::unordered_map<StringInterner::Id, StringInterner::Id>* summonPetToOwner
    );

    // Build damage taken index from source ActorMap
    void buildDamageTakenIndex();

    // Build healing received index from source ActorMap
    void buildHealingReceivedIndex();

    // Per-target damage weights resolved from MobWeightSettings.
    // Only targets weighted below 1 appear; empty = no weighting.
    // Mutable for the same reason timeSeriesCache_ is: it caches
    // derived data and gets refreshed through const query paths.
    mutable std::unordered_map<uint32_t, float> targetWeights_;

    // Weight multiplier for a target guid id (1.0 when unweighted)
    float targetWeight(uint32_t target_guid_id) const {
        if (targetWeights_.empty()) return 1.0f;
        auto it = targetWeights_.find(target_guid_id);
        return (it != targetWeights_.end()) ? it->second : 1.0f;
    }

    // Apply a target weight to a damage amount
    static int64_t applyWeight(int64_t amount, float weight) {
        return (weight >= 1.0f) ? amount
                                : static_cast<int64_t>(static_cast<double>(amount) * weight);
    }
};
