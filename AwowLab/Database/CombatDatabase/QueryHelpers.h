#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include "../CombatDatabase.h"

namespace combat_db {

// Calculate effective duration in seconds, clamped to encounter bounds
// Returns at least 0.001f to avoid division by zero
inline float calculateDurationSeconds(
    int32_t start_time_ms,
    int32_t end_time_ms,
    int32_t min_timestamp,
    int32_t max_timestamp
) {
    int32_t actual_start = std::max(start_time_ms, min_timestamp);
    int32_t actual_end = std::min(end_time_ms, max_timestamp);
    float duration = static_cast<float>(actual_end - actual_start) / 1000.0f;
    return duration < 0.001f ? 0.001f : duration;
}

// Sort results by effective_amount descending
inline void sortByEffectiveAmount(std::vector<ActorCombatStats>& results) {
    std::sort(results.begin(), results.end(),
        [](const ActorCombatStats& a, const ActorCombatStats& b) {
            return a.effective_amount > b.effective_amount;
        });
}

// Sort results by total_amount descending (for overhealing, etc.)
inline void sortByTotalAmount(std::vector<ActorCombatStats>& results) {
    std::sort(results.begin(), results.end(),
        [](const ActorCombatStats& a, const ActorCombatStats& b) {
            return a.total_amount > b.total_amount;
        });
}

// Calculate percentages based on effective_amount
inline void calculatePercentages(std::vector<ActorCombatStats>& results) {
    int64_t grand_total = 0;
    for (const auto& stats : results) {
        grand_total += stats.effective_amount;
    }

    if (grand_total > 0) {
        for (auto& stats : results) {
            stats.percent_of_total = static_cast<float>(stats.effective_amount)
                / static_cast<float>(grand_total) * 100.0f;
        }
    }
}

// Calculate percentages based on total_amount (for overhealing, etc.)
inline void calculatePercentagesByTotal(std::vector<ActorCombatStats>& results) {
    int64_t grand_total = 0;
    for (const auto& stats : results) {
        grand_total += stats.total_amount;
    }

    if (grand_total > 0) {
        for (auto& stats : results) {
            stats.percent_of_total = static_cast<float>(stats.total_amount)
                / static_cast<float>(grand_total) * 100.0f;
        }
    }
}

// Limit results to max_results entries
inline void limitResults(std::vector<ActorCombatStats>& results, size_t max_results) {
    if (results.size() > max_results) {
        results.resize(max_results);
    }
}

// Combined finalize: sort by effective, calculate percentages, limit
inline void finalizeResults(
    std::vector<ActorCombatStats>& results,
    size_t max_results
) {
    sortByEffectiveAmount(results);
    calculatePercentages(results);
    limitResults(results, max_results);
}

// Combined finalize: sort by total, calculate percentages by total, limit
inline void finalizeResultsByTotal(
    std::vector<ActorCombatStats>& results,
    size_t max_results
) {
    sortByTotalAmount(results);
    calculatePercentagesByTotal(results);
    limitResults(results, max_results);
}

} // namespace combat_db
