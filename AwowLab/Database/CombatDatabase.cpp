#include "CombatDatabase.h"
#include "Core/StringInterner.h"
#include "Core/MobWeightSettings.h"
#include <algorithm>
#include <iostream>

void CombatDatabase::loadFromActorMap(const ActorMap* actorMap) {
    actorMap_ = actorMap;
    min_timestamp_ = UINT32_MAX;
    max_timestamp_ = 0;
    petToOwnerMap_.clear();
    ownerToPetsMap_.clear();
    timeSeriesCache_.clear();

    if (!actorMap_ || actorMap_->empty()) {
        return;
    }

    // Find time bounds by scanning all combat tables
    for (const auto& [guid, table] : *actorMap_) {
        auto updateBounds = [this](const std::vector<CombatRecord>& records) {
            if (!records.empty()) {
                min_timestamp_ = std::min(min_timestamp_, records.front().timestamp_ms);
                max_timestamp_ = std::max(max_timestamp_, records.back().timestamp_ms);
            }
        };

        updateBounds(table.damage_dealt_table);
        updateBounds(table.healing_done_table);
    }

    if (min_timestamp_ == UINT32_MAX) {
        min_timestamp_ = 0;
    }

    // Build pet-to-owner mapping
    buildPetToOwnerMap();

    // Build reverse index: owner -> pets (for efficient pet iteration)
    for (const auto& [pet_id, owner_id] : petToOwnerMap_) {
        ownerToPetsMap_[owner_id].push_back(pet_id);
    }

#ifndef NDEBUG
    std::cout << "[COMBAT DB] Built owner-to-pets map with " << ownerToPetsMap_.size() << " owners" << std::endl;
#endif

    // Build damage taken index
    buildDamageTakenIndex();

    // Build healing received index
    buildHealingReceivedIndex();

    // Resolve per-target damage weights from the user's mob weighting
    refreshTargetWeights();
}

void CombatDatabase::refreshTargetWeights() const {
    targetWeights_.clear();
    timeSeriesCache_.clear();  // Cached series were built with old weights

    const auto& settings = MobWeightSettings::instance();
    if (!settings.enabled || settings.weights.empty()) {
        return;
    }

    // Every damage target appears as a key in the damage-taken index,
    // so one pass over it covers all guids the aggregators will see
    for (const auto& [target_guid_id, records] : damageTakenIndex_) {
        std::string_view guid = guidInterner().lookup(target_guid_id);
        uint32_t npcId = MobWeightSettings::npcIdFromGuid(guid);
        if (npcId == 0) continue;

        float weight = settings.weightFor(npcId);
        if (weight < 0.9995f) {
            targetWeights_[target_guid_id] = weight;
        }
    }
}

void CombatDatabase::buildPetToOwnerMap() {
    if (!actorMap_) return;

    // Scan all combat records to find owner_guid relationships.
    // Maps pets/guardians/totems to whoever owns them - a player OR an
    // enemy. Enemy-side ownership matters so the Enemy Damage meter folds
    // a boss's summons into the boss row (matching how the player meter
    // folds a hunter's pet into the hunter), and so clicking that enemy
    // opens a breakdown that already includes the summon's spells.
    for (const auto& [guid_id, table] : *actorMap_) {
        // Skip Players - they can't be pets/guardians
        // Only Pet-, Creature-, and Vehicle- GUIDs can have owners
        std::string_view guid_sv = guidInterner().lookup(guid_id);
        if (guid_sv.starts_with("Player-")) {
            continue;
        }

        auto scanTable = [this, guid_id, guid_sv](const std::vector<CombatRecord>& records) {
            for (const auto& record : records) {
                // A valid owner_guid means this actor is someone's summon.
                // owner_guid "0000000000000000" means no owner (independent creature).
                std::string_view owner_sv = guidInterner().lookup(record.owner_guid_id);
                if (!owner_sv.empty() &&
                    owner_sv != "0000000000000000" &&
                    record.owner_guid_id != guid_id) {  // guard against self-ownership
                    // Only add if not already mapped (first occurrence wins)
                    if (petToOwnerMap_.find(guid_id) == petToOwnerMap_.end()) {
                        petToOwnerMap_[guid_id] = record.owner_guid_id;
#ifndef NDEBUG
                        std::cout << "[PET->OWNER] " << guid_sv << " -> " << owner_sv << std::endl;
#endif
                    }
                    break;  // Only need one record to establish the relationship
                }
            }
        };

        scanTable(table.damage_dealt_table);
        scanTable(table.healing_done_table);
    }

#ifndef NDEBUG
    std::cout << "[COMBAT DB] Built pet-to-owner map with " << petToOwnerMap_.size() << " entries" << std::endl;
#endif
}

std::string CombatDatabase::getOwnerGuid(const std::string& pet_guid) const {
    // Read-only boundary: find() never grows the interner pool.
    StringInterner::Id pet_id = guidInterner().find(pet_guid);
    if (pet_id == StringInterner::INVALID) return "";

    auto it = petToOwnerMap_.find(pet_id);
    if (it == petToOwnerMap_.end()) return "";
    return std::string(guidInterner().lookup(it->second));
}

bool CombatDatabase::isPetOrGuardian(const std::string& actor_guid) const {
    StringInterner::Id actor_id = guidInterner().find(actor_guid);
    if (actor_id == StringInterner::INVALID) return false;
    return petToOwnerMap_.find(actor_id) != petToOwnerMap_.end();
}

const std::vector<CombatRecord>* CombatDatabase::getCombatTable(
    const ActorTable& actor_table,
    CombatMetricType type
) const {
    switch (type) {
        case CombatMetricType::DamageDealt:
            return &actor_table.damage_dealt_table;
        case CombatMetricType::HealingDone:
            return &actor_table.healing_done_table;
        default:
            return &actor_table.damage_dealt_table;
    }
}
