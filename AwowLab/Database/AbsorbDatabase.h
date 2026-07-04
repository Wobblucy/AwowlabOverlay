#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include "EventTypes.h"

// Per-actor absorb (damage prevented) statistics for meter display.
// Ranks by the ABSORBER (the actor who owns the shield), regardless of
// whose spell dealt the damage.
struct ActorAbsorbStats {
    std::string actor_guid;   // Absorber GUID (shield owner)
    int64_t total_absorbed = 0;
    uint32_t event_count = 0;
    float percent_of_total = 0.0f;

    struct SpellBreakdown {
        uint32_t absorb_spell_id;  // The shield spell
        std::string spell_name;
        int64_t total_absorbed;
        uint32_t event_count;
    };
    std::vector<SpellBreakdown> spell_breakdown;
};

// Facade over a stored vector of AbsorbEvent.
// Live overlay path: LiveLogSession accumulates AbsorbEvent as it parses,
// LiveCombatStats::refresh() calls loadFromEvents to rebuild this facade.
class AbsorbDatabase {
public:
    AbsorbDatabase() = default;
    ~AbsorbDatabase() = default;

    void loadFromEvents(const std::vector<AbsorbEvent>& events);
    void loadFromEvents(std::vector<AbsorbEvent>&& events);

    bool empty() const { return events_.empty(); }
    size_t size() const { return events_.size(); }

    int32_t getMinTimestamp() const { return min_timestamp_; }
    int32_t getMaxTimestamp() const { return max_timestamp_; }

    // Rank absorbers by total damage prevented in the time window.
    // If includeBreakdowns, each entry's spell_breakdown is populated with
    // per-shield-spell totals sorted by amount desc.
    std::vector<ActorAbsorbStats> getRankedByAbsorber(
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40,
        bool includeBreakdowns = false) const;

    // Sum of absorbed amounts in the window across all absorbers.
    int64_t getTotalAbsorbed(int32_t start_time_ms, int32_t end_time_ms) const;

private:
    void finalizeLoad();

    std::vector<AbsorbEvent> events_;  // Sorted by timestamp
    int32_t min_timestamp_ = 0;
    int32_t max_timestamp_ = 0;
};
