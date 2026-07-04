#include "DispelDatabase.h"
#include <algorithm>

// Internal helper to complete initialization after events are loaded
void DispelDatabase::finalizeLoad() {
    std::sort(events_.begin(), events_.end(),
        [](const DispelInterruptEvent& a, const DispelInterruptEvent& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });

    // Build timestamp bounds
    min_timestamp_ = events_.front().timestamp_ms;
    max_timestamp_ = events_.back().timestamp_ms;

    // Build source index for efficient actor queries
    for (size_t i = 0; i < events_.size(); ++i) {
        sourceIndex_[events_[i].source_guid].push_back(i);
    }
}

void DispelDatabase::loadFromEvents(const std::vector<DispelInterruptEvent>& events) {
    events_.clear();
    sourceIndex_.clear();

    if (events.empty()) {
        min_timestamp_ = 0;
        max_timestamp_ = 0;
        return;
    }

    // Copy events
    events_ = events;
    finalizeLoad();
}

void DispelDatabase::loadFromEvents(std::vector<DispelInterruptEvent>&& events) {
    events_.clear();
    sourceIndex_.clear();

    if (events.empty()) {
        min_timestamp_ = 0;
        max_timestamp_ = 0;
        return;
    }

    // Move events (no allocation)
    events_ = std::move(events);
    finalizeLoad();
}

std::vector<const DispelInterruptEvent*> DispelDatabase::getEventsInRange(
    int32_t start_time_ms, int32_t end_time_ms) const {

    std::vector<const DispelInterruptEvent*> result;

    // Binary search for start position
    auto startIt = std::lower_bound(events_.begin(), events_.end(), start_time_ms,
        [](const DispelInterruptEvent& e, int32_t time) {
            return e.timestamp_ms < time;
        });

    // Iterate until we pass end time
    for (auto it = startIt; it != events_.end() && it->timestamp_ms <= end_time_ms; ++it) {
        result.push_back(&(*it));
    }

    return result;
}

std::vector<ActorDispelStats> DispelDatabase::getRankedByActor(
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results,
    bool includeBreakdowns,
    FilterType filter) const {

    std::unordered_map<std::string, ActorDispelStats> actorStats;
    uint32_t grandTotal = 0;

    // Aggregate stats per actor
    for (const auto& event : events_) {
        if (event.timestamp_ms < start_time_ms || event.timestamp_ms > end_time_ms) {
            continue;
        }

        // Apply filter
        bool includeEvent = false;
        switch (filter) {
            case FilterType::All:
                includeEvent = true;
                break;
            case FilterType::DispelsOnly:
                includeEvent = (event.event_type == DispelInterruptEvent::EventType::Dispel ||
                               event.event_type == DispelInterruptEvent::EventType::Stolen);
                break;
            case FilterType::InterruptsOnly:
                includeEvent = (event.event_type == DispelInterruptEvent::EventType::Interrupt);
                break;
        }

        if (!includeEvent) {
            continue;
        }

        auto& stats = actorStats[event.source_guid];
        stats.actor_guid = event.source_guid;

        switch (event.event_type) {
            case DispelInterruptEvent::EventType::Dispel:
                stats.dispel_count++;
                stats.total_count++;
                grandTotal++;
                break;
            case DispelInterruptEvent::EventType::Interrupt:
                stats.interrupt_count++;
                stats.total_count++;
                grandTotal++;
                break;
            case DispelInterruptEvent::EventType::Stolen:
                stats.stolen_count++;
                stats.total_count++;
                grandTotal++;
                break;
            case DispelInterruptEvent::EventType::DispelFailed:
                stats.failed_count++;
                // Failed dispels don't count toward total
                break;
        }

        // Build spell breakdown if requested
        if (includeBreakdowns && event.extra_spell_id > 0) {
            // Find or create breakdown entry
            auto it = std::find_if(stats.spell_breakdown.begin(), stats.spell_breakdown.end(),
                [&event](const ActorDispelStats::SpellBreakdown& b) {
                    return b.extra_spell_id == event.extra_spell_id &&
                           b.event_type == event.event_type;
                });

            if (it != stats.spell_breakdown.end()) {
                it->count++;
            } else {
                ActorDispelStats::SpellBreakdown breakdown;
                breakdown.extra_spell_id = event.extra_spell_id;
                breakdown.extra_spell_name = event.extra_spell_name;
                breakdown.count = 1;
                breakdown.event_type = event.event_type;
                stats.spell_breakdown.push_back(std::move(breakdown));
            }
        }
    }

    // Convert to vector and calculate percentages
    std::vector<ActorDispelStats> result;
    result.reserve(actorStats.size());

    for (auto& [guid, stats] : actorStats) {
        if (stats.total_count > 0) {  // Only include actors with successful actions
            stats.percent_of_total = grandTotal > 0
                ? (static_cast<float>(stats.total_count) / grandTotal) * 100.0f
                : 0.0f;

            // Sort spell breakdown by count if included
            if (includeBreakdowns && !stats.spell_breakdown.empty()) {
                std::sort(stats.spell_breakdown.begin(), stats.spell_breakdown.end(),
                    [](const ActorDispelStats::SpellBreakdown& a,
                       const ActorDispelStats::SpellBreakdown& b) {
                        return a.count > b.count;
                    });
            }

            result.push_back(std::move(stats));
        }
    }

    // Sort by total count (highest first)
    std::sort(result.begin(), result.end(),
        [](const ActorDispelStats& a, const ActorDispelStats& b) {
            return a.total_count > b.total_count;
        });

    // Limit results
    if (result.size() > max_results) {
        result.resize(max_results);
    }

    return result;
}

uint32_t DispelDatabase::getTotalDispelCount(int32_t start_time_ms, int32_t end_time_ms) const {
    if (events_.empty()) return 0;

    // Use binary search to find the start of the time range
    auto startIt = std::lower_bound(events_.begin(), events_.end(), start_time_ms,
        [](const DispelInterruptEvent& e, int32_t time) {
            return e.timestamp_ms < time;
        });

    uint32_t count = 0;
    // Iterate only within the time window
    for (auto it = startIt; it != events_.end() && it->timestamp_ms <= end_time_ms; ++it) {
        if (it->event_type == DispelInterruptEvent::EventType::Dispel ||
            it->event_type == DispelInterruptEvent::EventType::Stolen) {
            count++;
        }
    }
    return count;
}

uint32_t DispelDatabase::getTotalInterruptCount(int32_t start_time_ms, int32_t end_time_ms) const {
    if (events_.empty()) return 0;

    // Use binary search to find the start of the time range
    auto startIt = std::lower_bound(events_.begin(), events_.end(), start_time_ms,
        [](const DispelInterruptEvent& e, int32_t time) {
            return e.timestamp_ms < time;
        });

    uint32_t count = 0;
    // Iterate only within the time window
    for (auto it = startIt; it != events_.end() && it->timestamp_ms <= end_time_ms; ++it) {
        if (it->event_type == DispelInterruptEvent::EventType::Interrupt) {
            count++;
        }
    }
    return count;
}
