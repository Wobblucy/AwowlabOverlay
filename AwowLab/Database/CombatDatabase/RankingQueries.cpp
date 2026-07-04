#include "../CombatDatabase.h"
#include "QueryHelpers.h"
#include "Core/StringInterner.h"
#include "Core/MobWeightSettings.h"
#include "Structures/CombatTypes.h"
#include <algorithm>
#include <unordered_map>

std::vector<ActorCombatStats> CombatDatabase::getRankedByActor(
    CombatMetricType type,
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results
) const {
    if (!actorMap_ || actorMap_->empty()) {
        return {};
    }

    std::vector<ActorCombatStats> results;
    results.reserve(actorMap_->size());

    float duration_seconds = combat_db::calculateDurationSeconds(
        start_time_ms, end_time_ms, min_timestamp_, max_timestamp_);

    // Aggregate for each actor
    for (const auto& [guid, table] : *actorMap_) {
        const auto* combat_table = getCombatTable(table, type);
        if (!combat_table || combat_table->empty()) {
            continue;
        }

        auto stats = aggregateStats(guid, *combat_table, start_time_ms, end_time_ms);
        if (stats.total_amount > 0 || stats.hit_count > 0) {
            stats.amount_per_second = static_cast<float>(stats.effective_amount) / duration_seconds;
            results.push_back(std::move(stats));
        }
    }

    combat_db::finalizeResults(results, max_results);
    return results;
}

std::vector<SpellCombatStats> CombatDatabase::getSpellBreakdown(
    const std::string& actor_guid,
    CombatMetricType type,
    int32_t start_time_ms,
    int32_t end_time_ms
) const {
    // The synthetic metrics have their own aggregation paths since
    // they filter across tables rather than reading a single table.
    if (type == CombatMetricType::FriendlyFire) {
        return getFriendlyFireSpellBreakdown(actor_guid, start_time_ms, end_time_ms);
    }
    if (type == CombatMetricType::HealingReceived) {
        return getHealingReceivedSpellBreakdown(actor_guid, start_time_ms, end_time_ms);
    }

    if (!actorMap_) {
        return {};
    }

    // Read-only boundary: an unknown guid just means no data.
    StringInterner::Id actor_id = guidInterner().find(actor_guid);
    if (actor_id == StringInterner::INVALID) {
        return {};
    }

    auto it = actorMap_->find(actor_id);
    if (it == actorMap_->end()) {
        return {};
    }

    const auto* combat_table = getCombatTable(it->second, type);
    if (!combat_table || combat_table->empty()) {
        return {};
    }

    return aggregateSpellBreakdown(*combat_table, start_time_ms, end_time_ms);
}

std::vector<ActorCombatStats> CombatDatabase::getCumulative(
    CombatMetricType type,
    int32_t current_time_ms,
    size_t max_results
) const {
    return getRankedByActor(type, 0, current_time_ms, max_results);
}

std::vector<ActorCombatStats> CombatDatabase::getFullEncounter(
    CombatMetricType type,
    size_t max_results
) const {
    return getRankedByActor(type, 0, INT32_MAX, max_results);
}

int64_t CombatDatabase::getGrandTotal(
    CombatMetricType type,
    int32_t start_time_ms,
    int32_t end_time_ms
) const {
    if (!actorMap_ || actorMap_->empty()) {
        return 0;
    }

    int64_t total = 0;

    for (const auto& [guid, table] : *actorMap_) {
        const auto* combat_table = getCombatTable(table, type);
        if (!combat_table || combat_table->empty()) {
            continue;
        }

        // Binary search for start position
        auto start_it = std::lower_bound(combat_table->begin(), combat_table->end(), start_time_ms,
            [](const CombatRecord& r, int32_t ts) { return r.timestamp_ms < ts; });

        for (auto it = start_it; it != combat_table->end() && it->timestamp_ms <= end_time_ms; ++it) {
            total += it->amount;
        }
    }

    return total;
}

std::vector<ActorCombatStats> CombatDatabase::getRankedByActorWithPets(
    CombatMetricType type,
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results,
    bool includeBreakdowns,
    const std::unordered_set<uint32_t>* blacklistedSpells
) const {
    if (!actorMap_ || actorMap_->empty()) {
        return {};
    }

    // Special GUID for the virtual blacklist actor
    static const std::string BLACKLIST_GUID = "Blacklist-0-0-0-0";

    // Map to accumulate stats: key is the "effective owner" guid id
    // For pets/guardians, this is their owner; for players/independents, it's themselves
    std::unordered_map<StringInterner::Id, ActorCombatStats> aggregatedStats;

    // Blacklist actor for aggregating blacklisted spell damage
    ActorCombatStats blacklistStats;
    blacklistStats.actor_guid = BLACKLIST_GUID;
    std::unordered_map<uint32_t, SpellCombatStats> blacklistSpellBreakdown;

    float duration_seconds = combat_db::calculateDurationSeconds(
        start_time_ms, end_time_ms, min_timestamp_, max_timestamp_);

    // Aggregate for each actor, merging pets into their owners
    // When blacklist is provided, filter at record level
    for (const auto& [guid, table] : *actorMap_) {
        const auto* combat_table = getCombatTable(table, type);
        if (!combat_table || combat_table->empty()) {
            continue;
        }

        // Determine who to attribute this damage to
        StringInterner::Id effective_owner = guid;  // Not a pet, attribute to self
        auto ownerIt = petToOwnerMap_.find(guid);
        if (ownerIt != petToOwnerMap_.end()) {
            effective_owner = ownerIt->second;
        }

        // Binary search for start position
        auto start_it = std::lower_bound(combat_table->begin(), combat_table->end(), start_time_ms,
            [](const CombatRecord& r, int32_t ts) { return r.timestamp_ms < ts; });

        // Iterate through time window, splitting between actor and blacklist
        for (auto it = start_it; it != combat_table->end() && it->timestamp_ms <= end_time_ms; ++it) {
            // Friendly fire counts as damage taken, not damage done
            if (it->flags & CombatEventFlags::FriendlyFire) {
                continue;
            }

            bool isBlacklisted = blacklistedSpells && blacklistedSpells->count(it->spell_id) > 0;
            bool isCrit = (it->flags & CombatEventFlags::Critical) != 0;

            // Mob weighting: damage to a discounted target contributes at
            // its configured fraction. Applied here too - this with-pets
            // path is what the meter's damage view actually calls, so
            // leaving it raw made weights look like they did nothing.
            const float w = targetWeight(it->target_guid_id);
            const int64_t amt = applyWeight(it->amount, w);
            const int64_t eff = applyWeight(it->effective_amount, w);

            if (isBlacklisted) {
                // Add to blacklist actor
                blacklistStats.total_amount += amt;
                blacklistStats.effective_amount += eff;
                blacklistStats.hit_count++;
                if (isCrit) {
                    blacklistStats.crit_count++;
                }

                // Track spell breakdown for blacklist actor
                auto& spellEntry = blacklistSpellBreakdown[it->spell_id];
                spellEntry.spell_id = it->spell_id;
                spellEntry.spell_school = it->spell_school;
                spellEntry.total_amount += amt;
                spellEntry.effective_amount += eff;
                spellEntry.hit_count++;
                if (isCrit) {
                    spellEntry.crit_count++;
                }
                spellEntry.max_hit = std::max(spellEntry.max_hit, amt);
                spellEntry.min_hit = std::min(spellEntry.min_hit, amt);
            } else {
                // Add to the effective owner's accumulated stats
                auto& ownerEntry = aggregatedStats[effective_owner];
                if (ownerEntry.actor_guid.empty()) {
                    ownerEntry.actor_guid = std::string(guidInterner().lookup(effective_owner));
                }
                ownerEntry.total_amount += amt;
                ownerEntry.effective_amount += eff;
                ownerEntry.hit_count++;
                if (isCrit) {
                    ownerEntry.crit_count++;
                }
            }
        }
    }

    // Only populate breakdowns if requested (expensive operation)
    // Meter panels don't need breakdowns for list display, only for expanded view
    if (includeBreakdowns) {
        // Populate breakdowns for each owner
        for (auto& [owner_guid, ownerStats] : aggregatedStats) {
            if (ownerStats.total_amount == 0 && ownerStats.hit_count == 0) {
                continue;
            }

            // Get pet breakdown for this owner
            ownerStats.pet_breakdown = aggregatePetBreakdown(owner_guid, type, start_time_ms, end_time_ms);

            // Owner's own spells stay flat in spell_breakdown; each pet TYPE
            // becomes its own collapsible group in pet_spell_groups. Targets
            // still merge owner + pets into one target_breakdown.
            std::unordered_map<uint32_t, SpellCombatStats> ownSpells;
            std::unordered_map<std::string, TargetCombatStats> combinedTargets;

            // Merge one aggregated spell into a per-spell_id accumulator.
            auto mergeSpell = [](std::unordered_map<uint32_t, SpellCombatStats>& into,
                                 const SpellCombatStats& spell) {
                auto& entry = into[spell.spell_id];
                entry.spell_id = spell.spell_id;
                entry.spell_school = spell.spell_school;
                entry.total_amount += spell.total_amount;
                entry.effective_amount += spell.effective_amount;
                entry.hit_count += spell.hit_count;
                entry.crit_count += spell.crit_count;
                entry.max_hit = std::max(entry.max_hit, spell.max_hit);
                if (spell.min_hit < entry.min_hit) entry.min_hit = spell.min_hit;
                // Merge normal/crit breakdown
                entry.normal_count += spell.normal_count;
                entry.normal_total += spell.normal_total;
                if (spell.normal_min < entry.normal_min) entry.normal_min = spell.normal_min;
                entry.normal_max = std::max(entry.normal_max, spell.normal_max);
                entry.crit_total += spell.crit_total;
                if (spell.crit_min < entry.crit_min) entry.crit_min = spell.crit_min;
                entry.crit_max = std::max(entry.crit_max, spell.crit_max);
            };

            // Add owner's own spells and targets
            auto owner_it = actorMap_->find(owner_guid);
            if (owner_it != actorMap_->end()) {
                const auto* owner_table = getCombatTable(owner_it->second, type);
                if (owner_table && !owner_table->empty()) {
                    auto owner_spells = aggregateSpellBreakdown(*owner_table, start_time_ms, end_time_ms);
                    for (const auto& spell : owner_spells) {
                        mergeSpell(ownSpells, spell);
                    }

                    auto owner_targets = aggregateTargetBreakdown(*owner_table, start_time_ms, end_time_ms);
                    for (const auto& target : owner_targets) {
                        auto& entry = combinedTargets[target.target_guid];
                        entry.target_guid = target.target_guid;
                        entry.total_amount += target.total_amount;
                        entry.hit_count += target.hit_count;
                    }
                }
            }

            // Build one spell group per pet TYPE. Same-NPC-id spawns merge
            // into one group (all "Lesser Ghoul" copies -> one group); a
            // real named pet (npc id 0) keys on its own guid. Mirrors the
            // NPC-id grouping aggregatePetBreakdown already does for the
            // "damage by pet" summary rows.
            struct PetGroupAcc {
                std::string pet_guid;
                uint32_t npc_id = 0;
                std::unordered_map<uint32_t, SpellCombatStats> spells;
            };
            std::unordered_map<uint64_t, PetGroupAcc> petGroups;  // group key -> accumulator

            auto petIt = ownerToPetsMap_.find(owner_guid);
            if (petIt != ownerToPetsMap_.end()) {
                for (const auto& pet_guid : petIt->second) {
                    auto pet_it = actorMap_->find(pet_guid);
                    if (pet_it == actorMap_->end()) continue;

                    const auto* pet_table = getCombatTable(pet_it->second, type);
                    if (!pet_table || pet_table->empty()) continue;

                    // Group key: NPC id when the guid has one (summons), else
                    // the pet's own interned id so distinct real pets aren't
                    // collapsed together.
                    std::string_view pet_guid_sv = guidInterner().lookup(pet_guid);
                    uint32_t npcId = MobWeightSettings::npcIdFromGuid(pet_guid_sv);
                    uint64_t key = npcId != 0 ? (uint64_t{1} << 32 | npcId)
                                              : (uint64_t{2} << 32 | pet_guid);

                    auto& acc = petGroups[key];
                    if (acc.pet_guid.empty()) {
                        acc.pet_guid = std::string(pet_guid_sv);
                        acc.npc_id = npcId;
                    }

                    auto pet_spells = aggregateSpellBreakdown(*pet_table, start_time_ms, end_time_ms);
                    for (const auto& spell : pet_spells) {
                        mergeSpell(acc.spells, spell);
                    }

                    auto pet_targets = aggregateTargetBreakdown(*pet_table, start_time_ms, end_time_ms);
                    for (const auto& target : pet_targets) {
                        auto& entry = combinedTargets[target.target_guid];
                        entry.target_guid = target.target_guid;
                        entry.total_amount += target.total_amount;
                        entry.hit_count += target.hit_count;
                    }
                }
            }

            // Owner's own spells -> flat spell_breakdown (sorted by total).
            ownerStats.spell_breakdown.reserve(ownSpells.size());
            for (auto& [spell_id, spell_stats] : ownSpells) {
                ownerStats.spell_breakdown.push_back(std::move(spell_stats));
            }
            std::sort(ownerStats.spell_breakdown.begin(), ownerStats.spell_breakdown.end(),
                [](const SpellCombatStats& a, const SpellCombatStats& b) {
                    return a.total_amount > b.total_amount;
                });

            // Finalize pet groups: sort each group's spells, compute totals,
            // then order the groups by total damage descending.
            ownerStats.pet_spell_groups.reserve(petGroups.size());
            for (auto& [key, acc] : petGroups) {
                PetSpellGroup group;
                group.pet_guid = std::move(acc.pet_guid);
                group.npc_id = acc.npc_id;
                group.spells.reserve(acc.spells.size());
                for (auto& [spell_id, spell_stats] : acc.spells) {
                    group.total_amount += spell_stats.total_amount;
                    group.hit_count += spell_stats.hit_count;
                    group.spells.push_back(std::move(spell_stats));
                }
                std::sort(group.spells.begin(), group.spells.end(),
                    [](const SpellCombatStats& a, const SpellCombatStats& b) {
                        return a.total_amount > b.total_amount;
                    });
                ownerStats.pet_spell_groups.push_back(std::move(group));
            }
            std::sort(ownerStats.pet_spell_groups.begin(), ownerStats.pet_spell_groups.end(),
                [](const PetSpellGroup& a, const PetSpellGroup& b) {
                    return a.total_amount > b.total_amount;
                });

            // Convert target map to sorted vector
            ownerStats.target_breakdown.reserve(combinedTargets.size());
            for (auto& [guid, target_stats] : combinedTargets) {
                ownerStats.target_breakdown.push_back(std::move(target_stats));
            }
            std::sort(ownerStats.target_breakdown.begin(), ownerStats.target_breakdown.end(),
                [](const TargetCombatStats& a, const TargetCombatStats& b) {
                    return a.total_amount > b.total_amount;
                });
        }
    }

    // Convert to vector
    std::vector<ActorCombatStats> results;
    results.reserve(aggregatedStats.size() + 1);  // +1 for potential blacklist actor
    for (auto& [guid, stats] : aggregatedStats) {
        if (stats.total_amount > 0 || stats.hit_count > 0) {
            stats.amount_per_second = static_cast<float>(stats.effective_amount) / duration_seconds;
            results.push_back(std::move(stats));
        }
    }

    // Add blacklist actor if it has any damage
    if (blacklistStats.effective_amount > 0 || blacklistStats.hit_count > 0) {
        // Convert blacklist spell breakdown map to sorted vector
        blacklistStats.spell_breakdown.reserve(blacklistSpellBreakdown.size());
        for (auto& [spell_id, spellStats] : blacklistSpellBreakdown) {
            blacklistStats.spell_breakdown.push_back(std::move(spellStats));
        }
        std::sort(blacklistStats.spell_breakdown.begin(), blacklistStats.spell_breakdown.end(),
            [](const SpellCombatStats& a, const SpellCombatStats& b) {
                return a.effective_amount > b.effective_amount;
            });

        blacklistStats.amount_per_second = static_cast<float>(blacklistStats.effective_amount) / duration_seconds;
        results.push_back(std::move(blacklistStats));
    }

    combat_db::finalizeResults(results, max_results);
    return results;
}

std::vector<ActorCombatStats> CombatDatabase::getRankedByOverhealing(
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results
) const {
    if (!actorMap_ || actorMap_->empty()) {
        return {};
    }

    // Map to accumulate stats by owner guid id
    std::unordered_map<StringInterner::Id, ActorCombatStats> aggregatedStats;

    float duration_seconds = combat_db::calculateDurationSeconds(
        start_time_ms, end_time_ms, min_timestamp_, max_timestamp_);

    // Iterate through all actors' healing_done_table
    for (const auto& [guid, table] : *actorMap_) {
        if (table.healing_done_table.empty()) {
            continue;
        }

        // Determine who to attribute this healing to
        StringInterner::Id effective_owner = guid;
        auto ownerIt = petToOwnerMap_.find(guid);
        if (ownerIt != petToOwnerMap_.end()) {
            effective_owner = ownerIt->second;
        }

        // Binary search for start position
        const auto& heal_table = table.healing_done_table;
        auto start_it = std::lower_bound(heal_table.begin(), heal_table.end(), start_time_ms,
            [](const CombatRecord& r, int32_t ts) { return r.timestamp_ms < ts; });

        // Iterate through time window
        for (auto it = start_it; it != heal_table.end() && it->timestamp_ms <= end_time_ms; ++it) {
            // Overhealing = amount - effective_amount
            int64_t overheal = it->amount - it->effective_amount;
            if (overheal <= 0) continue;

            auto& ownerEntry = aggregatedStats[effective_owner];
            if (ownerEntry.actor_guid.empty()) {
                ownerEntry.actor_guid = std::string(guidInterner().lookup(effective_owner));
            }
            // Use total_amount to store overhealing
            ownerEntry.total_amount += overheal;
            ownerEntry.effective_amount += overheal;  // Same value for consistency
            ownerEntry.hit_count++;
        }
    }

    // Convert to vector
    std::vector<ActorCombatStats> results;
    results.reserve(aggregatedStats.size());
    for (auto& [guid, stats] : aggregatedStats) {
        if (stats.total_amount > 0) {
            stats.amount_per_second = static_cast<float>(stats.effective_amount) / duration_seconds;
            results.push_back(std::move(stats));
        }
    }

    combat_db::finalizeResultsByTotal(results, max_results);
    return results;
}

std::vector<ActorCombatStats> CombatDatabase::getRankedByHealingTaken(
    int32_t start_time_ms,
    int32_t end_time_ms,
    size_t max_results
) const {
    if (healingReceivedIndex_.empty()) {
        return {};
    }

    std::vector<ActorCombatStats> results;
    results.reserve(healingReceivedIndex_.size());

    float duration_seconds = combat_db::calculateDurationSeconds(
        start_time_ms, end_time_ms, min_timestamp_, max_timestamp_);

    // Aggregate for each target (recipient)
    for (const auto& [target_guid_id, recordPtrs] : healingReceivedIndex_) {
        // Lookup the GUID string from interned ID
        std::string_view target_guid_sv = guidInterner().lookup(target_guid_id);
        std::string target_guid(target_guid_sv);

        ActorCombatStats stats;
        stats.actor_guid = target_guid;

        // Aggregate from pointer records
        for (const CombatRecord* recordPtr : recordPtrs) {
            if (recordPtr->timestamp_ms < start_time_ms || recordPtr->timestamp_ms > end_time_ms) {
                continue;
            }

            stats.total_amount += recordPtr->amount;
            stats.effective_amount += recordPtr->effective_amount;
            stats.hit_count++;
            if (recordPtr->flags & CombatEventFlags::Critical) {
                stats.crit_count++;
            }
        }

        if (stats.effective_amount > 0 || stats.hit_count > 0) {
            stats.amount_per_second = static_cast<float>(stats.effective_amount) / duration_seconds;
            results.push_back(std::move(stats));
        }
    }

    combat_db::finalizeResults(results, max_results);
    return results;
}

std::vector<SpellCombatStats> CombatDatabase::getFriendlyFireSpellBreakdown(
    const std::string& actor_guid,
    int32_t start_time_ms,
    int32_t end_time_ms
) const {
    if (!actorMap_) return {};

    // Read-only boundary: an unknown guid just means no data.
    StringInterner::Id actor_id = guidInterner().find(actor_guid);
    if (actor_id == StringInterner::INVALID) return {};

    auto it = actorMap_->find(actor_id);
    if (it == actorMap_->end()) return {};

    // Collect the records the extractor flagged as friendly fire.
    // (Filtering by "target is a player" would also catch boss damage.)
    // Clear the flag on the copies so aggregateSpellBreakdown - which
    // skips friendly fire for damage-done queries - keeps them.
    std::vector<CombatRecord> filtered;
    filtered.reserve(16);
    for (const auto& r : it->second.damage_dealt_table) {
        if (r.timestamp_ms < start_time_ms || r.timestamp_ms > end_time_ms) continue;
        if (!(r.flags & CombatEventFlags::FriendlyFire)) continue;
        CombatRecord copy = r;
        copy.flags = static_cast<uint16_t>(copy.flags & ~CombatEventFlags::FriendlyFire);
        filtered.push_back(copy);
    }
    return aggregateSpellBreakdown(filtered, start_time_ms, end_time_ms);
}

std::vector<SpellCombatStats> CombatDatabase::getHealingReceivedSpellBreakdown(
    const std::string& actor_guid,
    int32_t start_time_ms,
    int32_t end_time_ms
) const {
    if (!actorMap_) return {};

    // "Healing received by X" isn't stored directly - every source
    // actor's healing_done_table carries the target guid. Walk all
    // sources and pull records that landed on this actor. This is a
    // lazy per-click query so we don't cache anything.
    // find() keeps this render-thread query from mutating the interner.
    StringInterner::Id target_id = guidInterner().find(actor_guid);
    if (target_id == StringInterner::INVALID) return {};
    std::vector<CombatRecord> collected;
    for (const auto& [source_guid, table] : *actorMap_) {
        for (const auto& r : table.healing_done_table) {
            if (r.timestamp_ms < start_time_ms || r.timestamp_ms > end_time_ms) continue;
            if (r.target_guid_id != target_id) continue;
            collected.push_back(r);
        }
    }
    std::sort(collected.begin(), collected.end(),
        [](const CombatRecord& a, const CombatRecord& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });
    return aggregateSpellBreakdown(collected, start_time_ms, end_time_ms);
}
