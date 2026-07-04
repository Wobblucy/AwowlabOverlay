#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include "structures.h"
#include "SpellCastEvent.h"
#include "AuraDatabase.h"
#include "Parser/WowClass.h"
#include "Parser/Include/Structures/CombatTypes.h"  // For MissType
#include "Parser/Include/Structures/Records.h"  // For CooldownReductionRecord
#include "Parser/Include/Parser/Parser_Combatant_Info.h"  // For TalentEntry

// ActorType: Derived from GUID prefix - used for geometry/rendering differentiation
// This is separate from UnitFlags which comes from combat log sourceFlags/destFlags
enum class ActorType {
    Player,     // GUID starts with "Player-"
    Creature,   // GUID starts with "Creature-"
    Vehicle,    // GUID starts with "Vehicle-"
    Pet,        // GUID starts with "Pet-"
    Unknown
};

struct ActorColor {
    float r, g, b;
};

// Compact actor event for memory-efficient storage in EventDatabase
// Uses interned IDs instead of strings (~24 bytes vs ~100 bytes per event)
// Note: Color is NOT stored per-event - use ActorColorGenerator for color lookup
struct ActorEventCompact {
    uint32_t guid_id;              // Interned GUID
    uint32_t name_id;              // Interned actor name
    int32_t timestamp_ms;          // Relative timestamp (negative = before encounter)
    float x_loc;                   // Raw world coordinate
    float y_loc;                   // Raw world coordinate
    uint16_t map_id;
    uint8_t actor_type;            // ActorType as uint8
    uint8_t flags_affiliation : 2; // UnitAffiliation (0-3)
    uint8_t flags_reaction : 2;    // UnitReaction (0-2)
    uint8_t flags_controller : 1;  // UnitController (0-1)
    uint8_t flags_unit_type : 3;   // UnitType (0-4)
    uint8_t padding;               // Alignment padding
    uint16_t facing_radians;       // radians * 10000 (0-62831)
};

// Note: Color is NOT stored per-event - use ActorColorGenerator for color lookup
struct ActorEvent {
    std::string_view source_guid;  // Unique identifier (GUID) - points to interned string
    std::string_view source_actor; // Display name - points to interned string
    ActorType actor_type = ActorType::Unknown;  // From GUID prefix (for geometry selection)
    UnitFlags flags{};             // From combat log flags (for filtering by reaction/affiliation)
    int32_t timestamp_ms = 0;      // Relative timestamp (negative = before encounter)
    float x_loc = 0.0f;            // Raw world coordinate (matches MAP_CHANGE bounds)
    float y_loc = 0.0f;            // Raw world coordinate (matches MAP_CHANGE bounds)
    uint16_t map_id = 0;           // Zone/instance ID (0 = not set)
    uint16_t facing_radians = 0;   // radians * 10000 (0-62831)
};

struct ResourceEvent {
    std::string actor_guid;        // GUID of unit whose health changed
    // Note: actor_name removed - look up from guidToNameMap when needed
    ActorType actor_type;          // From GUID prefix
    UnitFlags flags;               // Parsed unit flags
    int32_t timestamp_ms;          // Relative timestamp (negative = before encounter)
    uint64_t current_health;       // Health after event (uint64 for raid bosses)
    uint64_t max_health;           // Max health (uint64 for raid bosses)
    uint32_t current_power;        // Primary power after event
    uint32_t max_power;            // Max primary power
    int8_t power_type;             // PowerType enum value
};

// COMBATANT_INFO event - provides player spec and talent information at encounter start
struct CombatantInfoEvent {
    std::string player_guid;       // Player GUID
    uint16_t spec_id;              // SpecializationID (e.g., 62=Arcane Mage, 250=Blood DK)
    std::vector<parser::TalentEntry> talents;  // Talents from COMBATANT_INFO token 27+
    int32_t haste_melee = 0;       // Haste rating from imported combatant info (gear + buffs at fight start)
    int32_t haste_ranged = 0;      // Haste rating (ranged)
    int32_t haste_spell = 0;       // Haste rating (spell)
};

// MAP_CHANGE event - provides map boundaries when entering a new area
struct MapChangeEvent {
    int32_t timestamp_ms;          // Relative timestamp (negative = before encounter)
    uint32_t map_id;               // uiMapID (matches PNG filename)
    std::string map_name;          // Display name (e.g., "Manaforge Omega")
    float x1, y1;                  // Top-left world bounds
    float x2, y2;                  // Bottom-right world bounds
};

// SPELL_SUMMON event - tracks pet/guardian summoning for owner attribution
struct SummonEvent {
    std::string summoner_guid;     // Player/NPC who summoned
    std::string summoned_guid;     // Pet/Guardian/Creature that was summoned
    int32_t timestamp_ms;          // Relative timestamp (negative = before encounter)
    uint32_t spell_id;             // Summon spell ID
};

// UNIT_DIED / UNIT_DESTROYED / SPELL_INSTAKILL - unit death event
struct DeathEvent {
    std::string actor_guid;        // GUID of unit that died
    std::string actor_name;        // Display name
    ActorType actor_type;          // From GUID prefix
    int32_t timestamp_ms;          // Relative timestamp (negative = before encounter)
    uint32_t killing_spell_id;     // Spell that killed (from SPELL_INSTAKILL, 0 otherwise)
    std::string killing_spell_name; // Name of killing spell (empty if not SPELL_INSTAKILL)
};

// SPELL_RESURRECT - unit resurrection event
struct ResurrectEvent {
    std::string actor_guid;        // GUID of unit that was resurrected
    std::string actor_name;        // Display name
    int32_t timestamp_ms;          // Relative timestamp (negative = before encounter)
    uint32_t spell_id;             // Resurrect spell ID
};

// SPELL_MISSED / SWING_MISSED - attack that didn't deal damage
struct MissedEvent {
    std::string source_guid;       // Who attempted the attack
    std::string source_name;
    std::string target_guid;       // Who was targeted
    std::string target_name;
    uint32_t spell_id;             // Spell ID (0 for melee swing)
    std::string spell_name;
    int32_t timestamp_ms;          // Relative timestamp (negative = before encounter)
    MissType miss_type;            // DODGE, PARRY, MISS, ABSORB, etc.
    int64_t amount_missed;         // Amount that was absorbed/blocked (0 for dodge/parry/miss)
    int64_t blocked_amount;        // Blocked component for partial blocks
    bool is_offhand;               // Off-hand weapon attack
    bool is_critical;              // Would have been a crit
    bool is_periodic;              // From a DOT/HOT
};

// SPELL_ABSORBED - damage absorbed by a shield
struct AbsorbEvent {
    std::string source_guid;       // Who dealt the absorbed damage
    std::string source_name;
    std::string target_guid;       // Who received the damage (has the shield)
    std::string target_name;
    std::string absorber_guid;     // Who owns the shield (often same as target)
    std::string absorber_name;
    uint32_t source_spell_id;      // Attack spell that was absorbed (0 for melee)
    std::string source_spell_name;
    uint32_t absorb_spell_id;      // Shield spell that absorbed damage
    std::string absorb_spell_name;
    int32_t timestamp_ms;          // Relative timestamp (negative = before encounter)
    int64_t absorbed_amount;       // Amount absorbed by this shield
    int64_t total_damage;          // Total damage of the attack (before absorb)
    std::string supporter_guid;    // For SPELL_ABSORBED_SUPPORT (Aug Evoker)
};

// SPELL_DISPEL, SPELL_INTERRUPT, SPELL_STOLEN - dispel and interrupt events
struct DispelInterruptEvent {
    std::string source_guid;       // Who performed the dispel/interrupt
    std::string source_name;
    std::string target_guid;       // Owner of the dispelled/interrupted aura
    std::string target_name;
    int32_t timestamp_ms;          // Relative timestamp

    // The dispel/interrupt ability used
    uint32_t spell_id;             // Dispel/interrupt spell ID
    std::string spell_name;

    // What was dispelled/interrupted
    uint32_t extra_spell_id;       // Spell ID of dispelled/interrupted spell
    std::string extra_spell_name;
    uint32_t extra_spell_school;

    // Event classification
    enum class EventType : uint8_t {
        Dispel,          // SPELL_DISPEL - removed buff/debuff
        Interrupt,       // SPELL_INTERRUPT - interrupted cast
        Stolen,          // SPELL_STOLEN - stole buff (e.g., Spellsteal)
        DispelFailed     // SPELL_DISPEL_FAILED - dispel attempt failed
    };
    EventType event_type;

    AuraType aura_type;            // BUFF or DEBUFF (for dispels/stolen)
};

// Combat event - damage or healing event for metrics tracking
// Extracted from SPELL_DAMAGE, SPELL_HEAL, SWING_DAMAGE_LANDED, etc.
// Uses interned string IDs for memory efficiency
struct CombatEvent {
    uint32_t source_guid_id;       // Interned ID - Actor dealing damage/healing
    uint32_t target_guid_id;       // Interned ID - Actor receiving damage/healing
    uint32_t owner_guid_id;        // Interned ID - Owner GUID for pets/guardians (0 if not a pet)
    int32_t timestamp_ms;          // Relative timestamp (negative = before encounter)
    uint32_t spell_id;             // Spell ID (0 for melee auto-attack)
    int64_t amount;                // Raw damage/heal amount
    int64_t effective_amount;      // Amount - overkill/overheal
    int64_t absorbed;              // Absorbed component
    int64_t blocked;               // Blocked component
    int64_t resisted;              // Resisted component
    uint16_t flags;                // CombatEventFlags bitmask
    uint8_t spell_school;          // SpellSchool bitmask
    CombatEventType event_type;    // DamageDealt, HealingDone, etc.
};

// Spells seen per class during encounter (for UI spell assignment config)
struct SpellsSeenByClass {
    // WowClass (as uint8_t) -> { spell_id -> spell_name }
    std::unordered_map<uint8_t, std::unordered_map<uint32_t, std::string>> spellsByClass;

    void recordSpell(parser::WowClass wowClass, uint32_t spell_id, const std::string& spell_name) {
        if (wowClass == parser::WowClass::Unknown) return;
        auto classKey = static_cast<uint8_t>(wowClass);
        auto& classSpells = spellsByClass[classKey];
        // Only insert if not already present (preserve first name seen)
        if (classSpells.find(spell_id) == classSpells.end()) {
            classSpells[spell_id] = spell_name;
        }
    }

    const std::unordered_map<uint32_t, std::string>& getSpellsForClass(parser::WowClass wowClass) const {
        static const std::unordered_map<uint32_t, std::string> emptyMap;
        auto classKey = static_cast<uint8_t>(wowClass);
        auto it = spellsByClass.find(classKey);
        return (it != spellsByClass.end()) ? it->second : emptyMap;
    }

    std::vector<parser::WowClass> getSeenClasses() const {
        std::vector<parser::WowClass> result;
        result.reserve(spellsByClass.size());
        for (const auto& [classKey, _] : spellsByClass) {
            result.push_back(static_cast<parser::WowClass>(classKey));
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    void clear() { spellsByClass.clear(); }
    bool hasSpells() const {
        for (const auto& [_, spells] : spellsByClass) {
            if (!spells.empty()) return true;
        }
        return false;
    }
};

// Auras (buffs/debuffs) seen per class during encounter (for UI aura assignment config)
// Tracks auras by RECIPIENT's class (not source's class)
struct AurasSeenByClass {
    struct AuraInfo {
        std::string spell_name;
        bool is_buff;  // true = BUFF, false = DEBUFF
    };

    // WowClass (as uint8_t) -> { spell_id -> AuraInfo }
    std::unordered_map<uint8_t, std::unordered_map<uint32_t, AuraInfo>> aurasByClass;

    void recordAura(parser::WowClass wowClass, uint32_t spell_id,
                    const std::string& spell_name, bool is_buff) {
        if (wowClass == parser::WowClass::Unknown) return;
        auto classKey = static_cast<uint8_t>(wowClass);
        auto& classAuras = aurasByClass[classKey];
        // Only insert if not already present (preserve first occurrence)
        if (classAuras.find(spell_id) == classAuras.end()) {
            classAuras[spell_id] = AuraInfo{spell_name, is_buff};
        }
    }

    const std::unordered_map<uint32_t, AuraInfo>& getAurasForClass(parser::WowClass wowClass) const {
        static const std::unordered_map<uint32_t, AuraInfo> emptyMap;
        auto classKey = static_cast<uint8_t>(wowClass);
        auto it = aurasByClass.find(classKey);
        return (it != aurasByClass.end()) ? it->second : emptyMap;
    }

    std::vector<parser::WowClass> getSeenClasses() const {
        std::vector<parser::WowClass> result;
        result.reserve(aurasByClass.size());
        for (const auto& [classKey, _] : aurasByClass) {
            result.push_back(static_cast<parser::WowClass>(classKey));
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    void clear() { aurasByClass.clear(); }
    bool hasAuras() const {
        for (const auto& [_, auras] : aurasByClass) {
            if (!auras.empty()) return true;
        }
        return false;
    }
};

// Boss/unit emote line (EMOTE). Text is cleaned of UI escape
// sequences (textures, colors, spell links) at extraction so the
// same emote produces the same text on every pull - phase rules
// match against it exactly.
struct EmoteEvent {
    int32_t timestamp_ms = 0;
    std::string source_guid;
    std::string source_name;
    std::string text;
};

// Combined result from single-pass extraction
struct ExtractedData {
    std::vector<ActorEvent> actorEvents;
    std::vector<SpellCastEvent> spellCastEvents;
    std::vector<ResourceEvent> resourceEvents;
    std::vector<CombatantInfoEvent> combatantInfoEvents;
    std::vector<MapChangeEvent> mapChangeEvents;
    std::vector<CombatEvent> combatEvents;  // Damage and healing events
    std::vector<SummonEvent> summonEvents;  // Pet/guardian summon events
    std::vector<DeathEvent> deathEvents;    // Unit death events
    std::vector<ResurrectEvent> resurrectEvents;  // Resurrection events
    std::vector<AuraEvent> auraEvents;      // Buff/debuff events
    std::vector<MissedEvent> missedEvents;  // Missed attack events (DODGE, PARRY, ABSORB, etc.)
    std::vector<AbsorbEvent> absorbEvents;  // Shield absorption events
    std::vector<DispelInterruptEvent> dispelInterruptEvents;  // Dispel/interrupt events
    std::vector<CooldownReductionRecord> cooldownReductions;  // Resource-based CD reductions
    std::vector<EmoteEvent> emoteEvents;    // Boss/unit emote lines (phase rules read these)

    // Spell ID -> Spell Name mapping (built from parsed events for UI display)
    std::unordered_map<uint32_t, std::string> spellNameMap;

    // GUID -> Actor Name mapping (built from all combat events - source and target names)
    // Used to resolve pet/guardian names that don't appear in position events
    std::unordered_map<std::string, std::string> guidToNameMap;

    SpellsSeenByClass spellsSeenByClass;
    AurasSeenByClass aurasSeenByClass;

    // GUID -> minimum timestamp when this actor was first seen in any event
    // Used by AuraDatabase to backdate removal-only buffs to first-seen time
    std::unordered_map<std::string, int32_t> firstSeenTimestamps;

    // True when the data came from an imported report rather than a
    // local combat log. Imported data needs haste recalculated from
    // aura events.
    bool fromImportedReport = false;

    // Collect all unique spell IDs from all event types
    // Used for lazy-loading spell names from SpellNameDatabase
    std::unordered_set<uint32_t> collectAllSpellIds() const {
        std::unordered_set<uint32_t> ids;

        // From spellNameMap (already parsed from log)
        for (const auto& [id, _] : spellNameMap) {
            if (id != 0) ids.insert(id);
        }

        // From spell cast events
        for (const auto& e : spellCastEvents) {
            if (e.spell_id != 0) ids.insert(e.spell_id);
        }

        // From combat events (damage/healing)
        for (const auto& e : combatEvents) {
            if (e.spell_id != 0) ids.insert(e.spell_id);
        }

        // From aura events
        for (const auto& e : auraEvents) {
            if (e.spell_id != 0) ids.insert(e.spell_id);
        }

        // From missed events
        for (const auto& e : missedEvents) {
            if (e.spell_id != 0) ids.insert(e.spell_id);
        }

        // From absorb events
        for (const auto& e : absorbEvents) {
            if (e.source_spell_id != 0) ids.insert(e.source_spell_id);
            if (e.absorb_spell_id != 0) ids.insert(e.absorb_spell_id);
        }

        // From dispel/interrupt events
        for (const auto& e : dispelInterruptEvents) {
            if (e.spell_id != 0) ids.insert(e.spell_id);
            if (e.extra_spell_id != 0) ids.insert(e.extra_spell_id);
        }

        // From summon events
        for (const auto& e : summonEvents) {
            if (e.spell_id != 0) ids.insert(e.spell_id);
        }

        // From death events (killing spell)
        for (const auto& e : deathEvents) {
            if (e.killing_spell_id != 0) ids.insert(e.killing_spell_id);
        }

        // From resurrect events
        for (const auto& e : resurrectEvents) {
            if (e.spell_id != 0) ids.insert(e.spell_id);
        }

        return ids;
    }
};

// getActorTypeFromGuid is declared in Core/Utils/ColorUtils.h
// Include that header to use awow::getActorTypeFromGuid()
