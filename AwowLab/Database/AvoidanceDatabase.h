#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <algorithm>
#include "EventTypes.h"

// Per-actor avoidance statistics for meter display
// Tracks dodge/parry/block/miss stats per TARGET (who avoided damage)
struct ActorAvoidanceStats {
    std::string actor_guid;  // Target who avoided the damage
    uint32_t dodge_count = 0;
    uint32_t parry_count = 0;
    uint32_t block_count = 0;
    uint32_t miss_count = 0;
    uint32_t deflect_count = 0;
    uint32_t immune_count = 0;
    uint32_t resist_count = 0;
    uint32_t reflect_count = 0;
    uint32_t total_count = 0;  // Total avoidance events
    float percent_of_total = 0.0f;

    // Damage that would have landed but was absorbed / blocked / resisted.
    // The combat log only carries an amount for those three miss types -
    // pure dodge/parry/miss/immune have no amount recorded. total_amount
    // is the sum of the three below.
    int64_t total_amount = 0;
    int64_t absorb_amount = 0;
    int64_t block_amount = 0;
    int64_t resist_amount = 0;

    // Breakdown by miss type. count is populated for every type; amount
    // is only meaningful for ABSORB, BLOCK, RESIST.
    struct MissTypeBreakdown {
        MissType type;
        uint32_t count;
        int64_t amount = 0;
    };
    std::vector<MissTypeBreakdown> breakdown;
};

// Per-target drill-down: what a specific target avoided, aggregated
// both by attacker (which enemy) and by spell. Used by the avoidance
// breakdown modal.
struct AvoidanceBreakdown {
    std::string target_guid;
    int64_t total_amount = 0;
    uint32_t total_count = 0;

    struct SourceRow {
        std::string source_guid;
        std::string source_name;
        int64_t amount = 0;
        uint32_t count = 0;
    };
    struct SpellRow {
        uint32_t spell_id = 0;
        std::string spell_name;
        int64_t amount = 0;
        uint32_t count = 0;
        // Per-spell miss-type split so the row can annotate e.g.
        // "12 dodged, 4 absorbed for 1.2M".
        std::vector<ActorAvoidanceStats::MissTypeBreakdown> per_type;
    };

    std::vector<SourceRow> by_source;  // sorted by amount desc, count tiebreak
    std::vector<SpellRow> by_spell;    // same sort
};

// Database for querying avoidance (missed attack) events
// Operates as a facade over stored MissedEvent vector
class AvoidanceDatabase {
public:
    AvoidanceDatabase() = default;
    ~AvoidanceDatabase() = default;

    // Load events from extracted data (by copy)
    void loadFromEvents(const std::vector<MissedEvent>& events);

    // Load events from extracted data (by move for efficiency)
    void loadFromEvents(std::vector<MissedEvent>&& events);

    // Check if database is empty
    bool empty() const { return events_.empty(); }

    // Get total event count
    size_t size() const { return events_.size(); }

    // Get all events in time range
    std::vector<const MissedEvent*> getEventsInRange(
        int32_t start_time_ms, int32_t end_time_ms) const;

    // Filter types for avoidance queries
    enum class FilterType {
        All,           // All miss types
        AvoidanceOnly, // DODGE, PARRY, MISS only (pure avoidance)
        MitigationOnly // BLOCK, ABSORB, RESIST, DEFLECT
    };

    // Get targets ranked by total avoidance events (for meter display)
    // Ranks by who AVOIDED the most attacks (target perspective)
    std::vector<ActorAvoidanceStats> getRankedByTarget(
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40,
        bool includeBreakdowns = false,
        FilterType filter = FilterType::All) const;

    // Time bounds
    int32_t getMinTimestamp() const { return min_timestamp_; }
    int32_t getMaxTimestamp() const { return max_timestamp_; }

    // Get total avoidance count for header display
    uint32_t getTotalAvoidanceCount(int32_t start_time_ms, int32_t end_time_ms) const;

    // Get counts by specific miss type
    uint32_t getCountByMissType(MissType type, int32_t start_time_ms, int32_t end_time_ms) const;

    // Per-target drill-down. Walks the raw MissedEvent stream for the
    // given target within the window and buckets by source_guid and
    // spell_id. Callers own the returned aggregation; safe to hold
    // across frames until the next refresh() invalidates the DB.
    AvoidanceBreakdown getBreakdownForTarget(
        const std::string& target_guid,
        int32_t start_time_ms,
        int32_t end_time_ms) const;

private:
    // Internal helper to finalize load (sort, build indices)
    void finalizeLoad();

    std::vector<MissedEvent> events_;  // Sorted by timestamp
    int32_t min_timestamp_ = 0;
    int32_t max_timestamp_ = 0;

    // Index by target_guid for efficient target queries
    std::unordered_map<std::string, std::vector<size_t>> targetIndex_;
};
