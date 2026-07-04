#include "CombatDatabase.h"
#include "Core/StringInterner.h"
#include "Core/MobWeightSettings.h"
#include <algorithm>
#include <iostream>

void CombatDatabase::loadFromActorMap(
    const ActorMap* actorMap,
    const std::unordered_map<StringInterner::Id, StringInterner::Id>* summonPetToOwner
) {
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
    buildPetToOwnerMap(summonPetToOwner);

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

void CombatDatabase::buildPetToOwnerMap(
    const std::unordered_map<StringInterner::Id, StringInterner::Id>* summonPetToOwner
) {
    if (!actorMap_) return;

    // NPC-id -> summoning player, built from the summon lineage. WoW reuses
    // one NPC id across many spawns of the same summon (every "Lesser Ghoul"
    // or Army horseman shares an id, only the spawn suffix differs), and a
    // fresh spawn that reused an id can act before - or without - its own
    // SPELL_SUMMON line landing in this segment. Below we let such a spawn
    // inherit its NPC id's owner, but only when that id belongs to exactly
    // one summoning player, so a possessed/mind-controlled add (whose id no
    // player ever summoned) can never sneak into a player.
    std::unordered_map<uint32_t, StringInterner::Id> npcIdToSummoner;
    std::unordered_set<uint32_t> ambiguousNpcIds;
    if (summonPetToOwner) {
        for (const auto& [pet_id, owner_id] : *summonPetToOwner) {
            uint32_t npcId = MobWeightSettings::npcIdFromGuid(guidInterner().lookup(pet_id));
            if (npcId == 0) continue;
            auto [it, inserted] = npcIdToSummoner.try_emplace(npcId, owner_id);
            if (!inserted && it->second != owner_id) {
                ambiguousNpcIds.insert(npcId);  // summoned by more than one player
            }
        }
    }

    // Step 1: seed from the SPELL_SUMMON lineage. This is the trustworthy
    // signal for who owns a summoned unit - the summoner named it directly.
    // It's the only thing that lets a player claim a Creature-/Vehicle- pet
    // (a Death Knight's army of ghouls, a Magus of the Dead, an elemental)
    // whose own damage events ship with no owner in the advanced combat log.
    // We only seed entries whose owner actually appears in the log, so a
    // summon by someone who never shows up doesn't invent a phantom owner.
    if (summonPetToOwner) {
        for (const auto& [pet_id, owner_id] : *summonPetToOwner) {
            if (pet_id == owner_id) continue;                 // self-ownership
            if (actorMap_->find(pet_id) == actorMap_->end()) continue;  // pet never acted
            petToOwnerMap_[pet_id] = owner_id;
#ifndef NDEBUG
            std::cout << "[PET->OWNER summon] " << guidInterner().lookup(pet_id)
                      << " -> " << guidInterner().lookup(owner_id) << std::endl;
#endif
        }
    }

    // Step 2: scan combat records for the relationships the summon map
    // didn't cover. This is where enemy-side ownership comes from so the
    // Enemy Damage meter folds a boss's summons into the boss row (matching
    // how the player meter folds a hunter's pet into the hunter), and so
    // clicking that enemy opens a breakdown that already includes the
    // summon's spells.
    for (const auto& [guid_id, table] : *actorMap_) {
        // Already established from the (authoritative) summon lineage.
        if (petToOwnerMap_.find(guid_id) != petToOwnerMap_.end()) {
            continue;
        }

        // Skip Players - they can't be pets/guardians.
        // Only Pet-, Creature-, and Vehicle- GUIDs can have owners.
        std::string_view guid_sv = guidInterner().lookup(guid_id);
        if (guid_sv.starts_with("Player-")) {
            continue;
        }

        // A Player owner may only be adopted here onto a real pet (a Pet-
        // GUID). A boss that hits a player's pet, or a mind-controlled/
        // possessed add, surfaces a Player owner_guid on a Creature-/
        // Vehicle- actor; the record's owner_guid describes the source
        // unit's owner, so an enemy creature carrying a player owner is not
        // that player's summon. Folding it in anyway merged the enemy's
        // whole damage-done into the player - which is why a pet class saw
        // damage it took show up as damage done. Genuine player summons are
        // handled by the summon lineage in step 1, so a Creature reaching
        // this scan with a player owner is never one of them. Guardians/
        // totems on the enemy side use Creature-/Vehicle- and keep merging
        // into their (non-player) owner.
        const bool actorIsRealPet = guid_sv.starts_with("Pet-");

        // A Creature- pet that reused a known player-summon NPC id (an Army
        // horseman, a Magus of the Dead, a Lesser Ghoul) whose SPELL_SUMMON
        // didn't land for this exact spawn instance in this segment. WoW gives
        // every spawn of one summon the same NPC id, so when that id belongs
        // to exactly one summoning player, this spawn is that player's too -
        // even when its own damage events never carry an owner (the owner only
        // shows on its SPELL_CAST_SUCCESS advanced block, which isn't a combat
        // record). A possessed/mind-controlled add carries an NPC id no player
        // ever summoned, so it can never match here.
        StringInterner::Id summonedNpcOwner = 0;
        if (!actorIsRealPet) {
            uint32_t npcId = MobWeightSettings::npcIdFromGuid(guid_sv);
            if (npcId != 0 && ambiguousNpcIds.find(npcId) == ambiguousNpcIds.end()) {
                auto it = npcIdToSummoner.find(npcId);
                if (it != npcIdToSummoner.end()) summonedNpcOwner = it->second;
            }
        }

        // NPC-id summon lineage is authoritative for a Creature- pet - adopt
        // it directly without needing a per-record owner.
        if (summonedNpcOwner != 0) {
            petToOwnerMap_[guid_id] = summonedNpcOwner;
#ifndef NDEBUG
            std::cout << "[PET->OWNER npcid] " << guid_sv << " -> "
                      << guidInterner().lookup(summonedNpcOwner) << std::endl;
#endif
            continue;
        }

        // A pet's advanced-log owner_guid is only reliable on some events - a
        // Plaguestalker's SWING records name the player while its SPELL_DAMAGE
        // records can carry the target's info block instead (a stray Creature
        // owner). So we let a player owner win over any non-player owner
        // rather than trusting the first record we happen to hit. A player
        // owner is only adopted onto a real Pet- here (Creature- pets were
        // already resolved above via their summon NPC id). Everything else
        // (enemy guardians) takes the first valid non-player owner; a hostile/
        // possessed add carrying a player owner is rejected outright.
        StringInterner::Id playerOwner = 0;   // an accepted Player- owner
        StringInterner::Id fallbackOwner = 0; // first valid non-player owner

        auto scanTable = [&](const std::vector<CombatRecord>& records) {
            for (const auto& record : records) {
                if (record.owner_guid_id == guid_id) continue;  // self-ownership
                std::string_view owner_sv = guidInterner().lookup(record.owner_guid_id);
                if (owner_sv.empty() || owner_sv == "0000000000000000") {
                    continue;
                }

                if (owner_sv.starts_with("Player-")) {
                    // Real pets trust any player owner they name.
                    if (actorIsRealPet && playerOwner == 0) {
                        playerOwner = record.owner_guid_id;
                    }
                } else if (fallbackOwner == 0) {
                    fallbackOwner = record.owner_guid_id;
                }
            }
        };

        scanTable(table.damage_dealt_table);
        scanTable(table.healing_done_table);

        // Prefer the player owner (real pets); otherwise the enemy-side owner.
        StringInterner::Id owner = playerOwner != 0 ? playerOwner : fallbackOwner;
        if (owner != 0) {
            petToOwnerMap_[guid_id] = owner;
#ifndef NDEBUG
            std::cout << "[PET->OWNER] " << guid_sv << " -> "
                      << guidInterner().lookup(owner) << std::endl;
#endif
        }
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
