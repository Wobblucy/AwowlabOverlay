#include "../CombatDatabase.h"
#include "Core/StringInterner.h"
#include <algorithm>
#include <unordered_map>

ActorCombatStats CombatDatabase::aggregateStats(
    StringInterner::Id guid_id,
    const std::vector<CombatRecord>& records,
    int32_t start_time_ms,
    int32_t end_time_ms
) const {
    ActorCombatStats stats;
    stats.actor_guid = std::string(guidInterner().lookup(guid_id));

    if (records.empty()) {
        return stats;
    }

    // Binary search for start position
    auto start_it = std::lower_bound(records.begin(), records.end(), start_time_ms,
        [](const CombatRecord& r, int32_t ts) { return r.timestamp_ms < ts; });

    // Iterate through time window
    for (auto it = start_it; it != records.end() && it->timestamp_ms <= end_time_ms; ++it) {
        // Friendly fire counts as damage taken, not damage done
        if (it->flags & CombatEventFlags::FriendlyFire) {
            continue;
        }

        // Mob weighting: damage to a discounted target contributes at
        // its configured fraction (heal records never match a weight)
        const float w = targetWeight(it->target_guid_id);
        stats.total_amount += applyWeight(it->amount, w);
        stats.effective_amount += applyWeight(it->effective_amount, w);
        stats.hit_count++;

        if (it->flags & CombatEventFlags::Critical) {
            stats.crit_count++;
        }
    }

    return stats;
}

std::vector<SpellCombatStats> CombatDatabase::aggregateSpellBreakdown(
    const std::vector<CombatRecord>& records,
    int32_t start_time_ms,
    int32_t end_time_ms
) const {
    std::unordered_map<uint32_t, SpellCombatStats> spell_map;

    if (records.empty()) {
        return {};
    }

    // Binary search for start position
    auto start_it = std::lower_bound(records.begin(), records.end(), start_time_ms,
        [](const CombatRecord& r, int32_t ts) { return r.timestamp_ms < ts; });

    // Aggregate by spell_id
    for (auto it = start_it; it != records.end() && it->timestamp_ms <= end_time_ms; ++it) {
        // Friendly fire counts as damage taken, not damage done. The
        // Friendly Fire view strips this flag from its copies before
        // aggregating, so those still come through here.
        if (it->flags & CombatEventFlags::FriendlyFire) {
            continue;
        }

        // Mob weighting: damage to a discounted target contributes at
        // its configured fraction (heal records never match a weight)
        const float w = targetWeight(it->target_guid_id);
        const int64_t amt = applyWeight(it->amount, w);
        const int64_t eff = applyWeight(it->effective_amount, w);

        auto& spell_stats = spell_map[it->spell_id];
        spell_stats.spell_id = it->spell_id;
        spell_stats.spell_school = it->spell_school;
        spell_stats.total_amount += amt;
        spell_stats.effective_amount += eff;
        spell_stats.hit_count++;

        bool isCrit = (it->flags & CombatEventFlags::Critical) != 0;
        if (isCrit) {
            spell_stats.crit_count++;
            spell_stats.crit_total += amt;
            spell_stats.crit_min = std::min(spell_stats.crit_min, amt);
            spell_stats.crit_max = std::max(spell_stats.crit_max, amt);
        } else {
            spell_stats.normal_count++;
            spell_stats.normal_total += amt;
            spell_stats.normal_min = std::min(spell_stats.normal_min, amt);
            spell_stats.normal_max = std::max(spell_stats.normal_max, amt);
        }

        if (it->flags & CombatEventFlags::Periodic) {
            spell_stats.periodic_count++;
        }

        auto bumpBucket = [&](SpellCombatStats::HitTypeStats& b, int64_t amount) {
            b.count++;
            b.total += amount;
            b.min = std::min(b.min, amount);
            b.max = std::max(b.max, amount);
        };
        if (it->flags & CombatEventFlags::Glancing) {
            bumpBucket(spell_stats.glance_hits, amt);
        }
        if (it->resisted > 0 || (it->flags & CombatEventFlags::Resisted)) {
            bumpBucket(spell_stats.resisted_hits, it->resisted > 0 ? it->resisted : amt);
        }
        if (it->blocked > 0 || (it->flags & CombatEventFlags::Blocked)) {
            bumpBucket(spell_stats.blocked_hits, it->blocked > 0 ? it->blocked : amt);
        }
        if (it->absorbed > 0 || (it->flags & CombatEventFlags::Absorbed)) {
            bumpBucket(spell_stats.absorbed_hits, it->absorbed > 0 ? it->absorbed : amt);
        }

        spell_stats.max_hit = std::max(spell_stats.max_hit, amt);
        spell_stats.min_hit = std::min(spell_stats.min_hit, amt);
    }

    // Convert to vector and sort by total amount
    std::vector<SpellCombatStats> result;
    result.reserve(spell_map.size());
    for (auto& [spell_id, stats] : spell_map) {
        result.push_back(std::move(stats));
    }

    std::sort(result.begin(), result.end(),
        [](const SpellCombatStats& a, const SpellCombatStats& b) {
            return a.total_amount > b.total_amount;
        });

    return result;
}

std::vector<TargetCombatStats> CombatDatabase::aggregateTargetBreakdown(
    const std::vector<CombatRecord>& records,
    int32_t start_time_ms,
    int32_t end_time_ms
) const {
    // Use interned ID as key to avoid string allocations in hot path
    std::unordered_map<uint32_t, TargetCombatStats> target_map;

    if (records.empty()) {
        return {};
    }

    // Binary search for start position
    auto start_it = std::lower_bound(records.begin(), records.end(), start_time_ms,
        [](const CombatRecord& r, int32_t ts) { return r.timestamp_ms < ts; });

    // Aggregate by target_guid_id (interned ID, no string allocation)
    for (auto it = start_it; it != records.end() && it->timestamp_ms <= end_time_ms; ++it) {
        if (it->target_guid_id == 0) continue;

        // Friendly fire counts as damage taken, not damage done
        if (it->flags & CombatEventFlags::FriendlyFire) continue;

        auto& target_stats = target_map[it->target_guid_id];
        target_stats.total_amount += applyWeight(it->amount, targetWeight(it->target_guid_id));
        target_stats.hit_count++;
    }

    // Convert to vector and resolve GUIDs only for final result
    std::vector<TargetCombatStats> result;
    result.reserve(target_map.size());
    for (auto& [guid_id, stats] : target_map) {
        std::string_view guid_sv = guidInterner().lookup(guid_id);
        if (!guid_sv.empty()) {
            stats.target_guid = std::string(guid_sv);
            result.push_back(std::move(stats));
        }
    }

    std::sort(result.begin(), result.end(),
        [](const TargetCombatStats& a, const TargetCombatStats& b) {
            return a.total_amount > b.total_amount;
        });

    return result;
}

std::vector<PetCombatStats> CombatDatabase::aggregatePetBreakdown(
    StringInterner::Id owner_guid_id,
    CombatMetricType type,
    int32_t start_time_ms,
    int32_t end_time_ms
) const {
    std::vector<PetCombatStats> result;

    if (!actorMap_) return result;

    // Use ownerToPetsMap_ for O(1) lookup instead of O(n) scan of petToOwnerMap_
    auto petIt = ownerToPetsMap_.find(owner_guid_id);
    if (petIt == ownerToPetsMap_.end()) return result;

    // Iterate only over this owner's pets
    for (StringInterner::Id pet_id : petIt->second) {
        // Get combat table for this pet
        auto actor_it = actorMap_->find(pet_id);
        if (actor_it == actorMap_->end()) continue;

        const auto* combat_table = getCombatTable(actor_it->second, type);
        if (!combat_table || combat_table->empty()) continue;

        // Aggregate damage from this pet
        auto stats = aggregateStats(pet_id, *combat_table, start_time_ms, end_time_ms);
        if (stats.total_amount > 0) {
            PetCombatStats pet_stats;
            pet_stats.pet_guid = std::move(stats.actor_guid);
            pet_stats.total_amount = stats.total_amount;
            pet_stats.hit_count = stats.hit_count;
            result.push_back(std::move(pet_stats));
        }
    }

    // Sort by total amount descending
    std::sort(result.begin(), result.end(),
        [](const PetCombatStats& a, const PetCombatStats& b) {
            return a.total_amount > b.total_amount;
        });

    return result;
}
