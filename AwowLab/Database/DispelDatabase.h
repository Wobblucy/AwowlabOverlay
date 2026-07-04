#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <algorithm>
#include "EventTypes.h"

// Per-actor dispel/interrupt statistics for meter display
struct ActorDispelStats {
    std::string actor_guid;
    uint32_t dispel_count = 0;      // SPELL_DISPEL events
    uint32_t interrupt_count = 0;   // SPELL_INTERRUPT events
    uint32_t stolen_count = 0;      // SPELL_STOLEN events
    uint32_t failed_count = 0;      // SPELL_DISPEL_FAILED events
    uint32_t total_count = 0;       // Total actions (dispel + interrupt + stolen)
    float percent_of_total = 0.0f;  // Percentage of total group actions

    // Breakdown by spell that was dispelled/interrupted
    struct SpellBreakdown {
        uint32_t extra_spell_id;    // What was dispelled/interrupted
        std::string extra_spell_name;
        uint32_t count;
        DispelInterruptEvent::EventType event_type;  // Type of action (dispel/interrupt/stolen)
    };
    std::vector<SpellBreakdown> spell_breakdown;
};

// Database for querying dispel and interrupt events
// Operates as a facade over stored DispelInterruptEvent vector
class DispelDatabase {
public:
    DispelDatabase() = default;
    ~DispelDatabase() = default;

    // Load events from extracted data (by copy)
    void loadFromEvents(const std::vector<DispelInterruptEvent>& events);

    // Load events from extracted data (by move for efficiency)
    void loadFromEvents(std::vector<DispelInterruptEvent>&& events);

    // Check if database is empty
    bool empty() const { return events_.empty(); }

    // Get total event count
    size_t size() const { return events_.size(); }

    // Get all events in time range
    std::vector<const DispelInterruptEvent*> getEventsInRange(
        int32_t start_time_ms, int32_t end_time_ms) const;

    // Filter type for getRankedByActor
    enum class FilterType { All, DispelsOnly, InterruptsOnly };

    // Get actors ranked by total dispel+interrupt count (for meter display)
    std::vector<ActorDispelStats> getRankedByActor(
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40,
        bool includeBreakdowns = false,
        FilterType filter = FilterType::All) const;

    // Time bounds
    int32_t getMinTimestamp() const { return min_timestamp_; }
    int32_t getMaxTimestamp() const { return max_timestamp_; }

    // Get total event counts for header display
    uint32_t getTotalDispelCount(int32_t start_time_ms, int32_t end_time_ms) const;
    uint32_t getTotalInterruptCount(int32_t start_time_ms, int32_t end_time_ms) const;

private:
    // Internal helper to finalize load (sort, build indices)
    void finalizeLoad();

    std::vector<DispelInterruptEvent> events_;  // Sorted by timestamp
    int32_t min_timestamp_ = 0;
    int32_t max_timestamp_ = 0;

    // Index by source_guid for efficient actor queries
    std::unordered_map<std::string, std::vector<size_t>> sourceIndex_;
};
