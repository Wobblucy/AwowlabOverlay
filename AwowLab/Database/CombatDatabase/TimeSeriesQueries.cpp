#include "../CombatDatabase.h"
#include "Core/StringInterner.h"
#include <algorithm>
#include <unordered_map>

std::vector<DamageTimeBucket> CombatDatabase::getActorDamageTimeSeries(
    const std::string& actor_guid,
    uint32_t bucket_size_ms,
    const std::unordered_set<uint32_t>* blacklistedSpells
) const {
    if (!actorMap_ || actorMap_->empty() || bucket_size_ms == 0) {
        return {};
    }

    // Check cache first (keyed by actor_guid + bucket_size)
    // Note: We skip cache when blacklist is provided since it can change
    std::string cacheKey = actor_guid + "_" + std::to_string(bucket_size_ms);
    if (!blacklistedSpells || blacklistedSpells->empty()) {
        auto cacheIt = timeSeriesCache_.find(cacheKey);
        if (cacheIt != timeSeriesCache_.end()) {
            return cacheIt->second;
        }
    }

    std::vector<DamageTimeBucket> result;

    // Read-only boundary: an unknown guid means no data.
    StringInterner::Id actor_id = guidInterner().find(actor_guid);
    if (actor_id == StringInterner::INVALID) {
        return result;
    }

    // Collect all damage records for this actor and their pets
    std::vector<const CombatRecord*> allRecords;

    // Get actor's own records
    auto actor_it = actorMap_->find(actor_id);
    if (actor_it != actorMap_->end()) {
        for (const auto& record : actor_it->second.damage_dealt_table) {
            // Friendly fire counts as damage taken, not damage done
            if (record.flags & CombatEventFlags::FriendlyFire) continue;
            allRecords.push_back(&record);
        }
    }

    // Get pet records using O(1) lookup instead of O(all_pets)
    auto petIt = ownerToPetsMap_.find(actor_id);
    if (petIt != ownerToPetsMap_.end()) {
        for (StringInterner::Id pet_id : petIt->second) {
            auto pet_it = actorMap_->find(pet_id);
            if (pet_it != actorMap_->end()) {
                for (const auto& record : pet_it->second.damage_dealt_table) {
                    if (record.flags & CombatEventFlags::FriendlyFire) continue;
                    allRecords.push_back(&record);
                }
            }
        }
    }

    if (allRecords.empty()) {
        return result;
    }

    // Sort by timestamp
    std::sort(allRecords.begin(), allRecords.end(),
        [](const CombatRecord* a, const CombatRecord* b) {
            return a->timestamp_ms < b->timestamp_ms;
        });

    // Calculate number of buckets
    uint32_t numBuckets = (max_timestamp_ - min_timestamp_ + bucket_size_ms - 1) / bucket_size_ms;
    if (numBuckets == 0) numBuckets = 1;
    if (numBuckets > 10000) numBuckets = 10000;  // Safety limit

    // Initialize buckets
    result.resize(numBuckets);
    for (uint32_t i = 0; i < numBuckets; ++i) {
        result[i].timestamp_ms = min_timestamp_ + i * bucket_size_ms;
    }

    // Aggregate damage into buckets, spreading each event over 3 buckets for smoother visualization
    // Distribution: 25% to bucket-1, 50% to center bucket, 25% to bucket+1
    std::vector<std::unordered_map<uint32_t, int64_t>> bucketSpellMaps(numBuckets);

    for (const auto* record : allRecords) {
        if (record->timestamp_ms < min_timestamp_) continue;

        // Skip blacklisted spells
        if (blacklistedSpells && blacklistedSpells->count(record->spell_id) > 0) {
            continue;
        }

        uint32_t centerIndex = (record->timestamp_ms - min_timestamp_) / bucket_size_ms;
        if (centerIndex >= numBuckets) centerIndex = numBuckets - 1;

        // Spread damage: 25% / 50% / 25% across 3 buckets.
        // Mob weighting applies here too so the chart matches the meter.
        int64_t amount = applyWeight(record->amount, targetWeight(record->target_guid_id));
        int64_t centerAmount = amount / 2;           // 50% to center
        int64_t sideAmount = amount / 4;             // 25% to each side
        int64_t remainder = amount - centerAmount - 2 * sideAmount;  // Handle rounding
        centerAmount += remainder;  // Add any rounding remainder to center

        // Add to center bucket
        result[centerIndex].total_damage += centerAmount;
        bucketSpellMaps[centerIndex][record->spell_id] += centerAmount;

        // Add to previous bucket (if valid)
        if (centerIndex > 0) {
            result[centerIndex - 1].total_damage += sideAmount;
            bucketSpellMaps[centerIndex - 1][record->spell_id] += sideAmount;
        }

        // Add to next bucket (if valid)
        if (centerIndex + 1 < numBuckets) {
            result[centerIndex + 1].total_damage += sideAmount;
            bucketSpellMaps[centerIndex + 1][record->spell_id] += sideAmount;
        }
    }

    // Convert spell maps to sorted vectors
    for (uint32_t i = 0; i < numBuckets; ++i) {
        auto& bucket = result[i];
        auto& spellMap = bucketSpellMaps[i];

        bucket.by_spell.reserve(spellMap.size());
        for (const auto& [spell_id, damage] : spellMap) {
            bucket.by_spell.emplace_back(spell_id, damage);
        }

        // Sort by damage descending
        std::sort(bucket.by_spell.begin(), bucket.by_spell.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
    }

    // Store in cache for future calls (only when no blacklist)
    if (!blacklistedSpells || blacklistedSpells->empty()) {
        timeSeriesCache_[cacheKey] = result;
    }

    return result;
}

std::vector<DamageTimeBucket> CombatDatabase::getActorHealingTimeSeries(
    const std::string& actor_guid,
    uint32_t bucket_size_ms,
    const std::unordered_set<uint32_t>* blacklistedSpells
) const {
    if (!actorMap_ || actorMap_->empty() || bucket_size_ms == 0) {
        return {};
    }

    // Check cache first (keyed by actor_guid + bucket_size + "heal" suffix)
    // Note: We skip cache when blacklist is provided since it can change
    std::string cacheKey = actor_guid + "_heal_" + std::to_string(bucket_size_ms);
    if (!blacklistedSpells || blacklistedSpells->empty()) {
        auto cacheIt = timeSeriesCache_.find(cacheKey);
        if (cacheIt != timeSeriesCache_.end()) {
            return cacheIt->second;
        }
    }

    std::vector<DamageTimeBucket> result;

    // Read-only boundary: an unknown guid means no data.
    StringInterner::Id actor_id = guidInterner().find(actor_guid);
    if (actor_id == StringInterner::INVALID) {
        return result;
    }

    // Collect all healing records for this actor and their pets
    std::vector<const CombatRecord*> allRecords;

    // Get actor's own records
    auto actor_it = actorMap_->find(actor_id);
    if (actor_it != actorMap_->end()) {
        for (const auto& record : actor_it->second.healing_done_table) {
            allRecords.push_back(&record);
        }
    }

    // Get pet records using O(1) lookup instead of O(all_pets)
    auto petIt = ownerToPetsMap_.find(actor_id);
    if (petIt != ownerToPetsMap_.end()) {
        for (StringInterner::Id pet_id : petIt->second) {
            auto pet_it = actorMap_->find(pet_id);
            if (pet_it != actorMap_->end()) {
                for (const auto& record : pet_it->second.healing_done_table) {
                    allRecords.push_back(&record);
                }
            }
        }
    }

    if (allRecords.empty()) {
        return result;
    }

    // Sort by timestamp
    std::sort(allRecords.begin(), allRecords.end(),
        [](const CombatRecord* a, const CombatRecord* b) {
            return a->timestamp_ms < b->timestamp_ms;
        });

    // Calculate number of buckets
    uint32_t numBuckets = (max_timestamp_ - min_timestamp_ + bucket_size_ms - 1) / bucket_size_ms;
    if (numBuckets == 0) numBuckets = 1;
    if (numBuckets > 10000) numBuckets = 10000;  // Safety limit

    // Initialize buckets
    result.resize(numBuckets);
    for (uint32_t i = 0; i < numBuckets; ++i) {
        result[i].timestamp_ms = min_timestamp_ + i * bucket_size_ms;
    }

    // Aggregate healing into buckets, spreading each event over 3 buckets for smoother visualization
    // Distribution: 25% to bucket-1, 50% to center bucket, 25% to bucket+1
    std::vector<std::unordered_map<uint32_t, int64_t>> bucketSpellMaps(numBuckets);

    for (const auto* record : allRecords) {
        if (record->timestamp_ms < min_timestamp_) continue;

        // Skip blacklisted spells
        if (blacklistedSpells && blacklistedSpells->count(record->spell_id) > 0) {
            continue;
        }

        uint32_t centerIndex = (record->timestamp_ms - min_timestamp_) / bucket_size_ms;
        if (centerIndex >= numBuckets) centerIndex = numBuckets - 1;

        // Spread healing: 25% / 50% / 25% across 3 buckets
        int64_t amount = record->amount;
        int64_t centerAmount = amount / 2;           // 50% to center
        int64_t sideAmount = amount / 4;             // 25% to each side
        int64_t remainder = amount - centerAmount - 2 * sideAmount;  // Handle rounding
        centerAmount += remainder;  // Add any rounding remainder to center

        // Add to center bucket
        result[centerIndex].total_damage += centerAmount;
        bucketSpellMaps[centerIndex][record->spell_id] += centerAmount;

        // Add to previous bucket (if valid)
        if (centerIndex > 0) {
            result[centerIndex - 1].total_damage += sideAmount;
            bucketSpellMaps[centerIndex - 1][record->spell_id] += sideAmount;
        }

        // Add to next bucket (if valid)
        if (centerIndex + 1 < numBuckets) {
            result[centerIndex + 1].total_damage += sideAmount;
            bucketSpellMaps[centerIndex + 1][record->spell_id] += sideAmount;
        }
    }

    // Convert spell maps to sorted vectors
    for (uint32_t i = 0; i < numBuckets; ++i) {
        auto& bucket = result[i];
        auto& spellMap = bucketSpellMaps[i];

        bucket.by_spell.reserve(spellMap.size());
        for (const auto& [spell_id, amount] : spellMap) {
            bucket.by_spell.emplace_back(spell_id, amount);
        }

        // Sort by amount descending
        std::sort(bucket.by_spell.begin(), bucket.by_spell.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
    }

    // Store in cache for future calls (only when no blacklist)
    if (!blacklistedSpells || blacklistedSpells->empty()) {
        timeSeriesCache_[cacheKey] = result;
    }

    return result;
}

std::vector<DamageTimeBucket> CombatDatabase::getActorDamageTakenTimeSeries(
    const std::string& actor_guid,
    uint32_t bucket_size_ms,
    const std::unordered_set<uint32_t>* blacklistedSpells
) const {
    if (damageTakenIndex_.empty() || bucket_size_ms == 0) {
        return {};
    }

    // Check cache first (keyed by actor_guid + bucket_size + "taken" suffix)
    // Note: We skip cache when blacklist is provided since it can change
    std::string cacheKey = actor_guid + "_taken_" + std::to_string(bucket_size_ms);
    if (!blacklistedSpells || blacklistedSpells->empty()) {
        auto cacheIt = timeSeriesCache_.find(cacheKey);
        if (cacheIt != timeSeriesCache_.end()) {
            return cacheIt->second;
        }
    }

    std::vector<DamageTimeBucket> result;

    // Read-only boundary: an unknown guid means no damage taken.
    StringInterner::Id actor_guid_id = guidInterner().find(actor_guid);
    if (actor_guid_id == StringInterner::INVALID) {
        return result;
    }

    // Find damage taken records for this actor
    auto damageIt = damageTakenIndex_.find(actor_guid_id);
    if (damageIt == damageTakenIndex_.end() || damageIt->second.empty()) {
        return result;
    }

    const auto& allRecords = damageIt->second;

    // Calculate number of buckets
    uint32_t numBuckets = (max_timestamp_ - min_timestamp_ + bucket_size_ms - 1) / bucket_size_ms;
    if (numBuckets == 0) numBuckets = 1;
    if (numBuckets > 10000) numBuckets = 10000;  // Safety limit

    // Initialize buckets
    result.resize(numBuckets);
    for (uint32_t i = 0; i < numBuckets; ++i) {
        result[i].timestamp_ms = min_timestamp_ + i * bucket_size_ms;
    }

    // Aggregate damage taken into buckets, spreading each event over 3 buckets for smoother visualization
    // Distribution: 25% to bucket-1, 50% to center bucket, 25% to bucket+1
    std::vector<std::unordered_map<uint32_t, int64_t>> bucketSpellMaps(numBuckets);

    for (const auto* record : allRecords) {
        if (record->timestamp_ms < min_timestamp_) continue;

        // Skip blacklisted spells
        if (blacklistedSpells && blacklistedSpells->count(record->spell_id) > 0) {
            continue;
        }

        uint32_t centerIndex = (record->timestamp_ms - min_timestamp_) / bucket_size_ms;
        if (centerIndex >= numBuckets) centerIndex = numBuckets - 1;

        // Spread damage: 25% / 50% / 25% across 3 buckets
        int64_t amount = record->amount;
        int64_t centerAmount = amount / 2;           // 50% to center
        int64_t sideAmount = amount / 4;             // 25% to each side
        int64_t remainder = amount - centerAmount - 2 * sideAmount;  // Handle rounding
        centerAmount += remainder;  // Add any rounding remainder to center

        // Add to center bucket
        result[centerIndex].total_damage += centerAmount;
        bucketSpellMaps[centerIndex][record->spell_id] += centerAmount;

        // Add to previous bucket (if valid)
        if (centerIndex > 0) {
            result[centerIndex - 1].total_damage += sideAmount;
            bucketSpellMaps[centerIndex - 1][record->spell_id] += sideAmount;
        }

        // Add to next bucket (if valid)
        if (centerIndex + 1 < numBuckets) {
            result[centerIndex + 1].total_damage += sideAmount;
            bucketSpellMaps[centerIndex + 1][record->spell_id] += sideAmount;
        }
    }

    // Convert spell maps to sorted vectors
    for (uint32_t i = 0; i < numBuckets; ++i) {
        auto& bucket = result[i];
        auto& spellMap = bucketSpellMaps[i];

        bucket.by_spell.reserve(spellMap.size());
        for (const auto& [spell_id, damage] : spellMap) {
            bucket.by_spell.emplace_back(spell_id, damage);
        }

        // Sort by damage descending
        std::sort(bucket.by_spell.begin(), bucket.by_spell.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
    }

    // Store in cache for future calls (only when no blacklist)
    if (!blacklistedSpells || blacklistedSpells->empty()) {
        timeSeriesCache_[cacheKey] = result;
    }

    return result;
}
