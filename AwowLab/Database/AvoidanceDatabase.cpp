#include "AvoidanceDatabase.h"
#include <algorithm>

void AvoidanceDatabase::loadFromEvents(const std::vector<MissedEvent>& events) {
    events_ = events;
    finalizeLoad();
}

void AvoidanceDatabase::loadFromEvents(std::vector<MissedEvent>&& events) {
    events_ = std::move(events);
    finalizeLoad();
}

void AvoidanceDatabase::finalizeLoad() {
    if (events_.empty()) {
        min_timestamp_ = 0;
        max_timestamp_ = 0;
        targetIndex_.clear();
        return;
    }

    // Sort by timestamp
    std::sort(events_.begin(), events_.end(),
        [](const MissedEvent& a, const MissedEvent& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });

    min_timestamp_ = events_.front().timestamp_ms;
    max_timestamp_ = events_.back().timestamp_ms;

    // Build target index
    targetIndex_.clear();
    for (size_t i = 0; i < events_.size(); ++i) {
        targetIndex_[events_[i].target_guid].push_back(i);
    }
}

std::vector<const MissedEvent*> AvoidanceDatabase::getEventsInRange(
    int32_t start_time_ms, int32_t end_time_ms) const {

    std::vector<const MissedEvent*> result;

    for (const auto& event : events_) {
        if (event.timestamp_ms >= start_time_ms && event.timestamp_ms <= end_time_ms) {
            result.push_back(&event);
        }
    }

    return result;
}

std::vector<ActorAvoidanceStats> AvoidanceDatabase::getRankedByTarget(
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results,
    bool includeBreakdowns,
    FilterType filter) const {

    // Aggregate by target
    std::unordered_map<std::string, ActorAvoidanceStats> statsMap;

    // Helper to check if miss type matches filter
    auto matchesFilter = [filter](MissType type) {
        switch (filter) {
            case FilterType::AvoidanceOnly:
                return type == MissType::DODGE || type == MissType::PARRY || type == MissType::MISS;
            case FilterType::MitigationOnly:
                return type == MissType::BLOCK || type == MissType::ABSORB ||
                       type == MissType::RESIST || type == MissType::DEFLECT;
            case FilterType::All:
            default:
                return true;
        }
    };

    for (const auto& event : events_) {
        if (event.timestamp_ms < start_time_ms || event.timestamp_ms > end_time_ms) {
            continue;
        }

        if (!matchesFilter(event.miss_type)) {
            continue;
        }

        auto& stats = statsMap[event.target_guid];
        if (stats.actor_guid.empty()) {
            stats.actor_guid = event.target_guid;
        }

        switch (event.miss_type) {
            case MissType::DODGE:   stats.dodge_count++;   break;
            case MissType::PARRY:   stats.parry_count++;   break;
            case MissType::BLOCK:
                stats.block_count++;
                stats.block_amount += event.amount_missed;
                stats.total_amount += event.amount_missed;
                break;
            case MissType::MISS:    stats.miss_count++;    break;
            case MissType::DEFLECT: stats.deflect_count++; break;
            case MissType::IMMUNE:  stats.immune_count++;  break;
            case MissType::RESIST:
                stats.resist_count++;
                stats.resist_amount += event.amount_missed;
                stats.total_amount += event.amount_missed;
                break;
            case MissType::REFLECT: stats.reflect_count++; break;
            case MissType::ABSORB:
                // ABSORB miss_type comes through SPELL_MISSED/SWING_MISSED
                // when the whole hit was eaten by a shield. Full amount
                // sits in amount_missed. The Absorbs meter attributes
                // this to the shield owner; the Avoidance meter shows it
                // per victim since that's who "avoided" the hit.
                stats.absorb_amount += event.amount_missed;
                stats.total_amount += event.amount_missed;
                break;
            default: break;
        }
        stats.total_count++;
    }

    // Convert to vector and sort
    std::vector<ActorAvoidanceStats> results;
    results.reserve(statsMap.size());

    int64_t grandAmount = 0;
    uint32_t grandCount = 0;
    for (auto& [guid, stats] : statsMap) {
        grandAmount += stats.total_amount;
        grandCount += stats.total_count;
        results.push_back(std::move(stats));
    }

    // Sort by total amount avoided descending, with count as tiebreaker
    // so targets that only ate dodges/parries (amount=0) still surface in
    // a consistent order rather than jittering on the equal keys.
    std::sort(results.begin(), results.end(),
        [](const ActorAvoidanceStats& a, const ActorAvoidanceStats& b) {
            if (a.total_amount != b.total_amount) {
                return a.total_amount > b.total_amount;
            }
            return a.total_count > b.total_count;
        });

    // Limit results
    if (results.size() > max_results) {
        results.resize(max_results);
    }

    // Percent-of-total is based on the amount when any amount was
    // recorded in the window; otherwise fall back to count so pure-
    // dodge fights (no absorb/block) don't just show 0% everywhere.
    bool anyAmount = grandAmount > 0;

    for (auto& stats : results) {
        if (anyAmount) {
            stats.percent_of_total = grandAmount > 0
                ? (static_cast<float>(stats.total_amount) / static_cast<float>(grandAmount)) * 100.0f
                : 0.0f;
        } else {
            stats.percent_of_total = grandCount > 0
                ? (static_cast<float>(stats.total_count) / static_cast<float>(grandCount)) * 100.0f
                : 0.0f;
        }

        // Add breakdown if requested
        if (includeBreakdowns) {
            auto add = [&](MissType type, uint32_t count, int64_t amount) {
                if (count > 0) {
                    stats.breakdown.push_back({type, count, amount});
                }
            };
            add(MissType::DODGE,   stats.dodge_count,   0);
            add(MissType::PARRY,   stats.parry_count,   0);
            add(MissType::BLOCK,   stats.block_count,   stats.block_amount);
            add(MissType::MISS,    stats.miss_count,    0);
            add(MissType::DEFLECT, stats.deflect_count, 0);
            add(MissType::IMMUNE,  stats.immune_count,  0);
            add(MissType::RESIST,  stats.resist_count,  stats.resist_amount);
            add(MissType::REFLECT, stats.reflect_count, 0);
            // ABSORB gets its own row when we saw any absorb activity
            // even though the individual count isn't tracked separately
            // above - we use total_count minus the sum of tracked types
            // to avoid missing the "absorbed" bucket entirely.
            uint32_t knownCount = stats.dodge_count + stats.parry_count + stats.block_count
                                + stats.miss_count + stats.deflect_count + stats.immune_count
                                + stats.resist_count + stats.reflect_count;
            uint32_t absorbCount = (stats.total_count > knownCount)
                ? stats.total_count - knownCount
                : 0;
            add(MissType::ABSORB, absorbCount, stats.absorb_amount);

            // Sort breakdown by amount desc, then count desc so absorb-
            // heavy targets surface the amount first.
            std::sort(stats.breakdown.begin(), stats.breakdown.end(),
                [](const ActorAvoidanceStats::MissTypeBreakdown& a,
                   const ActorAvoidanceStats::MissTypeBreakdown& b) {
                    if (a.amount != b.amount) return a.amount > b.amount;
                    return a.count > b.count;
                });
        }
    }

    return results;
}

uint32_t AvoidanceDatabase::getTotalAvoidanceCount(
    int32_t start_time_ms, int32_t end_time_ms) const {

    uint32_t count = 0;
    for (const auto& event : events_) {
        if (event.timestamp_ms >= start_time_ms && event.timestamp_ms <= end_time_ms) {
            ++count;
        }
    }
    return count;
}

uint32_t AvoidanceDatabase::getCountByMissType(
    MissType type, int32_t start_time_ms, int32_t end_time_ms) const {

    uint32_t count = 0;
    for (const auto& event : events_) {
        if (event.timestamp_ms >= start_time_ms && event.timestamp_ms <= end_time_ms &&
            event.miss_type == type) {
            ++count;
        }
    }
    return count;
}

AvoidanceBreakdown AvoidanceDatabase::getBreakdownForTarget(
    const std::string& target_guid,
    int32_t start_time_ms,
    int32_t end_time_ms) const {

    AvoidanceBreakdown result;
    result.target_guid = target_guid;

    // Aggregate into intermediate maps keyed by source GUID and spell
    // ID so we can build the sorted rows in one pass at the end.
    struct SourceAgg {
        std::string name;
        int64_t amount = 0;
        uint32_t count = 0;
    };
    struct SpellAgg {
        std::string name;
        int64_t amount = 0;
        uint32_t count = 0;
        // Per miss-type count and amount for the tooltip / row annotation.
        std::unordered_map<uint8_t, std::pair<uint32_t, int64_t>> per_type;
    };
    std::unordered_map<std::string, SourceAgg> sources;
    std::unordered_map<uint32_t, SpellAgg> spells;

    for (const auto& event : events_) {
        if (event.target_guid != target_guid) continue;
        if (event.timestamp_ms < start_time_ms || event.timestamp_ms > end_time_ms) continue;

        int64_t amount = event.amount_missed;  // 0 for pure dodge/parry/miss
        result.total_amount += amount;
        result.total_count++;

        auto& s = sources[event.source_guid];
        if (s.name.empty() && !event.source_name.empty()) s.name = event.source_name;
        s.amount += amount;
        s.count++;

        auto& sp = spells[event.spell_id];
        if (sp.name.empty() && !event.spell_name.empty()) sp.name = event.spell_name;
        sp.amount += amount;
        sp.count++;
        auto& bucket = sp.per_type[static_cast<uint8_t>(event.miss_type)];
        bucket.first++;
        bucket.second += amount;
    }

    // Materialize sorted vectors. Amount desc, count as tiebreaker so
    // dodge-heavy rows still land in a stable order.
    result.by_source.reserve(sources.size());
    for (auto& [guid, agg] : sources) {
        AvoidanceBreakdown::SourceRow row;
        row.source_guid = guid;
        row.source_name = std::move(agg.name);
        row.amount = agg.amount;
        row.count = agg.count;
        result.by_source.push_back(std::move(row));
    }
    std::sort(result.by_source.begin(), result.by_source.end(),
        [](const AvoidanceBreakdown::SourceRow& a,
           const AvoidanceBreakdown::SourceRow& b) {
            if (a.amount != b.amount) return a.amount > b.amount;
            return a.count > b.count;
        });

    result.by_spell.reserve(spells.size());
    for (auto& [spellId, agg] : spells) {
        AvoidanceBreakdown::SpellRow row;
        row.spell_id = spellId;
        row.spell_name = std::move(agg.name);
        row.amount = agg.amount;
        row.count = agg.count;
        for (auto& [typeVal, cnt_amt] : agg.per_type) {
            ActorAvoidanceStats::MissTypeBreakdown t;
            t.type = static_cast<MissType>(typeVal);
            t.count = cnt_amt.first;
            t.amount = cnt_amt.second;
            row.per_type.push_back(t);
        }
        std::sort(row.per_type.begin(), row.per_type.end(),
            [](const ActorAvoidanceStats::MissTypeBreakdown& a,
               const ActorAvoidanceStats::MissTypeBreakdown& b) {
                if (a.amount != b.amount) return a.amount > b.amount;
                return a.count > b.count;
            });
        result.by_spell.push_back(std::move(row));
    }
    std::sort(result.by_spell.begin(), result.by_spell.end(),
        [](const AvoidanceBreakdown::SpellRow& a,
           const AvoidanceBreakdown::SpellRow& b) {
            if (a.amount != b.amount) return a.amount > b.amount;
            return a.count > b.count;
        });

    return result;
}
