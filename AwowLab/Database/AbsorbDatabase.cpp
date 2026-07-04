#include "AbsorbDatabase.h"
#include <algorithm>

void AbsorbDatabase::loadFromEvents(const std::vector<AbsorbEvent>& events) {
    events_ = events;
    finalizeLoad();
}

void AbsorbDatabase::loadFromEvents(std::vector<AbsorbEvent>&& events) {
    events_ = std::move(events);
    finalizeLoad();
}

void AbsorbDatabase::finalizeLoad() {
    if (events_.empty()) {
        min_timestamp_ = 0;
        max_timestamp_ = 0;
        return;
    }

    std::sort(events_.begin(), events_.end(),
        [](const AbsorbEvent& a, const AbsorbEvent& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });

    min_timestamp_ = events_.front().timestamp_ms;
    max_timestamp_ = events_.back().timestamp_ms;
}

std::vector<ActorAbsorbStats> AbsorbDatabase::getRankedByAbsorber(
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results,
    bool includeBreakdowns) const {

    struct PerAbsorber {
        int64_t total = 0;
        uint32_t count = 0;
        // absorb_spell_id -> (amount, count, name)
        std::unordered_map<uint32_t, std::tuple<int64_t, uint32_t, std::string>> spells;
    };
    std::unordered_map<std::string, PerAbsorber> byAbsorber;

    for (const auto& event : events_) {
        if (event.timestamp_ms < start_time_ms || event.timestamp_ms > end_time_ms) {
            continue;
        }
        auto& p = byAbsorber[event.absorber_guid];
        p.total += event.absorbed_amount;
        p.count++;
        if (includeBreakdowns) {
            auto& [amt, cnt, name] = p.spells[event.absorb_spell_id];
            amt += event.absorbed_amount;
            cnt++;
            if (name.empty() && !event.absorb_spell_name.empty()) {
                name = event.absorb_spell_name;
            }
        }
    }

    std::vector<ActorAbsorbStats> results;
    results.reserve(byAbsorber.size());
    int64_t grandTotal = 0;
    for (auto& [guid, p] : byAbsorber) {
        ActorAbsorbStats stats;
        stats.actor_guid = guid;
        stats.total_absorbed = p.total;
        stats.event_count = p.count;
        grandTotal += p.total;
        if (includeBreakdowns) {
            stats.spell_breakdown.reserve(p.spells.size());
            for (auto& [spellId, tup] : p.spells) {
                auto& [amt, cnt, name] = tup;
                ActorAbsorbStats::SpellBreakdown sb;
                sb.absorb_spell_id = spellId;
                sb.spell_name = std::move(name);
                sb.total_absorbed = amt;
                sb.event_count = cnt;
                stats.spell_breakdown.push_back(std::move(sb));
            }
            std::sort(stats.spell_breakdown.begin(), stats.spell_breakdown.end(),
                [](const ActorAbsorbStats::SpellBreakdown& a,
                   const ActorAbsorbStats::SpellBreakdown& b) {
                    return a.total_absorbed > b.total_absorbed;
                });
        }
        results.push_back(std::move(stats));
    }

    std::sort(results.begin(), results.end(),
        [](const ActorAbsorbStats& a, const ActorAbsorbStats& b) {
            return a.total_absorbed > b.total_absorbed;
        });

    if (results.size() > max_results) {
        results.resize(max_results);
    }

    for (auto& stats : results) {
        stats.percent_of_total = grandTotal > 0
            ? (static_cast<float>(stats.total_absorbed) / static_cast<float>(grandTotal)) * 100.0f
            : 0.0f;
    }

    return results;
}

int64_t AbsorbDatabase::getTotalAbsorbed(int32_t start_time_ms, int32_t end_time_ms) const {
    int64_t total = 0;
    for (const auto& event : events_) {
        if (event.timestamp_ms >= start_time_ms && event.timestamp_ms <= end_time_ms) {
            total += event.absorbed_amount;
        }
    }
    return total;
}
