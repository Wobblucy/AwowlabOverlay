#include "UI/SpellGroupSettings.h"
#include "Core/UnifiedSettings.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>
#include <algorithm>
#include <iostream>

namespace spell_grouping {

// ============== SpecSpellGroups ==============

SpellGroup* SpecSpellGroups::findGroupContainingSpell(uint32_t spell_id) {
    for (auto& group : groups) {
        auto it = std::find(group.spell_ids.begin(), group.spell_ids.end(), spell_id);
        if (it != group.spell_ids.end()) {
            return &group;
        }
    }
    return nullptr;
}

const SpellGroup* SpecSpellGroups::findGroupContainingSpell(uint32_t spell_id) const {
    for (const auto& group : groups) {
        auto it = std::find(group.spell_ids.begin(), group.spell_ids.end(), spell_id);
        if (it != group.spell_ids.end()) {
            return &group;
        }
    }
    return nullptr;
}

SpellGroup* SpecSpellGroups::findGroupById(uint32_t group_id) {
    for (auto& group : groups) {
        if (group.group_id == group_id) {
            return &group;
        }
    }
    return nullptr;
}

const SpellGroup* SpecSpellGroups::findGroupById(uint32_t group_id) const {
    for (const auto& group : groups) {
        if (group.group_id == group_id) {
            return &group;
        }
    }
    return nullptr;
}

SpellGroup& SpecSpellGroups::createGroup(const std::string& name) {
    SpellGroup newGroup;
    newGroup.group_id = next_group_id++;
    newGroup.name = name;
    newGroup.collapsed = false;
    groups.push_back(std::move(newGroup));
    return groups.back();
}

void SpecSpellGroups::deleteGroup(uint32_t group_id) {
    groups.erase(
        std::remove_if(groups.begin(), groups.end(),
            [group_id](const SpellGroup& g) { return g.group_id == group_id; }),
        groups.end()
    );
}

bool SpecSpellGroups::addSpellToGroup(uint32_t group_id, uint32_t spell_id) {
    // First remove from any existing group
    removeSpellFromGroup(spell_id);

    // Find target group and add
    SpellGroup* group = findGroupById(group_id);
    if (group) {
        group->spell_ids.push_back(spell_id);
        return true;
    }
    return false;
}

bool SpecSpellGroups::removeSpellFromGroup(uint32_t spell_id) {
    for (auto& group : groups) {
        auto it = std::find(group.spell_ids.begin(), group.spell_ids.end(), spell_id);
        if (it != group.spell_ids.end()) {
            group.spell_ids.erase(it);
            return true;
        }
    }
    return false;
}

void SpecSpellGroups::moveSpellToGroup(uint32_t spell_id, uint32_t target_group_id) {
    // Remove from current group (if any)
    removeSpellFromGroup(spell_id);

    // Add to target group (if target_group_id > 0)
    if (target_group_id > 0) {
        addSpellToGroup(target_group_id, spell_id);
    }
}

// ============== SpellGroupSettings ==============

SpecSpellGroups& SpellGroupSettings::getOrCreateSpecGroups(uint16_t spec_id) {
    auto it = specGroups_.find(spec_id);
    if (it == specGroups_.end()) {
        SpecSpellGroups newGroups;
        newGroups.spec_id = spec_id;
        auto [inserted_it, _] = specGroups_.emplace(spec_id, std::move(newGroups));
        return inserted_it->second;
    }
    return it->second;
}

const SpecSpellGroups* SpellGroupSettings::getSpecGroups(uint16_t spec_id) const {
    auto it = specGroups_.find(spec_id);
    if (it != specGroups_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool SpellGroupSettings::isSpellGrouped(uint16_t spec_id, uint32_t spell_id) const {
    const auto* specGroups = getSpecGroups(spec_id);
    if (!specGroups) return false;
    return specGroups->findGroupContainingSpell(spell_id) != nullptr;
}

std::unordered_set<uint32_t> SpellGroupSettings::getGroupedSpellIds(uint16_t spec_id) const {
    std::unordered_set<uint32_t> result;
    const auto* specGroups = getSpecGroups(spec_id);
    if (specGroups) {
        for (const auto& group : specGroups->groups) {
            for (uint32_t spell_id : group.spell_ids) {
                result.insert(spell_id);
            }
        }
    }
    return result;
}

// ============== Global Blacklist ==============

bool SpellGroupSettings::isSpellBlacklisted(uint32_t spell_id) const {
    return blacklistedSpells_.find(spell_id) != blacklistedSpells_.end();
}

void SpellGroupSettings::blacklistSpell(uint32_t spell_id) {
    blacklistedSpells_.insert(spell_id);
}

void SpellGroupSettings::unblacklistSpell(uint32_t spell_id) {
    blacklistedSpells_.erase(spell_id);
}

std::string SpellGroupSettings::serializeToJson() const {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    rapidjson::Value specsArray(rapidjson::kArrayType);

    for (const auto& [specId, specGroups] : specGroups_) {
        rapidjson::Value specObj(rapidjson::kObjectType);
        specObj.AddMember("spec_id", specId, alloc);
        specObj.AddMember("next_group_id", specGroups.next_group_id, alloc);

        rapidjson::Value groupsArray(rapidjson::kArrayType);
        for (const auto& group : specGroups.groups) {
            rapidjson::Value groupObj(rapidjson::kObjectType);
            groupObj.AddMember("group_id", group.group_id, alloc);
            groupObj.AddMember("name", rapidjson::Value(group.name.c_str(), alloc), alloc);
            groupObj.AddMember("collapsed", group.collapsed, alloc);

            rapidjson::Value spellsArray(rapidjson::kArrayType);
            for (uint32_t spell_id : group.spell_ids) {
                spellsArray.PushBack(spell_id, alloc);
            }
            groupObj.AddMember("spell_ids", spellsArray, alloc);

            groupsArray.PushBack(groupObj, alloc);
        }
        specObj.AddMember("groups", groupsArray, alloc);

        specsArray.PushBack(specObj, alloc);
    }

    doc.AddMember("specs", specsArray, alloc);

    // Serialize blacklisted spells
    rapidjson::Value blacklistArray(rapidjson::kArrayType);
    for (uint32_t spell_id : blacklistedSpells_) {
        blacklistArray.PushBack(spell_id, alloc);
    }
    doc.AddMember("blacklisted_spells", blacklistArray, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

bool SpellGroupSettings::deserializeFromJson(const std::string& json) {
    if (json.empty()) {
        return true;  // Empty is valid - no groups yet
    }

    rapidjson::Document doc;
    doc.Parse(json.c_str());

    if (doc.HasParseError()) {
        std::cerr << "SpellGroupSettings JSON parse error: "
                  << rapidjson::GetParseError_En(doc.GetParseError()) << "\n";
        return false;
    }

    specGroups_.clear();
    blacklistedSpells_.clear();

    // Load blacklisted spells
    if (doc.HasMember("blacklisted_spells") && doc["blacklisted_spells"].IsArray()) {
        for (const auto& spellVal : doc["blacklisted_spells"].GetArray()) {
            if (spellVal.IsUint()) {
                blacklistedSpells_.insert(spellVal.GetUint());
            }
        }
    }

    if (!doc.HasMember("specs") || !doc["specs"].IsArray()) {
        return true;  // Valid but empty (blacklist may still have been loaded)
    }

    for (const auto& specVal : doc["specs"].GetArray()) {
        if (!specVal.IsObject()) continue;

        uint16_t specId = 0;
        if (specVal.HasMember("spec_id") && specVal["spec_id"].IsUint()) {
            specId = static_cast<uint16_t>(specVal["spec_id"].GetUint());
        }

        SpecSpellGroups specGroups;
        specGroups.spec_id = specId;

        if (specVal.HasMember("next_group_id") && specVal["next_group_id"].IsUint()) {
            specGroups.next_group_id = specVal["next_group_id"].GetUint();
        }

        if (specVal.HasMember("groups") && specVal["groups"].IsArray()) {
            for (const auto& groupVal : specVal["groups"].GetArray()) {
                if (!groupVal.IsObject()) continue;

                SpellGroup group;

                if (groupVal.HasMember("group_id") && groupVal["group_id"].IsUint()) {
                    group.group_id = groupVal["group_id"].GetUint();
                }

                if (groupVal.HasMember("name") && groupVal["name"].IsString()) {
                    group.name = groupVal["name"].GetString();
                }

                if (groupVal.HasMember("collapsed") && groupVal["collapsed"].IsBool()) {
                    group.collapsed = groupVal["collapsed"].GetBool();
                }

                if (groupVal.HasMember("spell_ids") && groupVal["spell_ids"].IsArray()) {
                    for (const auto& spellVal : groupVal["spell_ids"].GetArray()) {
                        if (spellVal.IsUint()) {
                            group.spell_ids.push_back(spellVal.GetUint());
                        }
                    }
                }

                specGroups.groups.push_back(std::move(group));
            }
        }

        specGroups_.emplace(specId, std::move(specGroups));
    }

    return true;
}

void SpellGroupSettings::loadFromSettings() {
    const auto& cache = SettingsCache::instance().get();
    if (!cache.spellGroupsJson.empty()) {
        deserializeFromJson(cache.spellGroupsJson);
    }
}

void SpellGroupSettings::saveToSettings() {
    auto& cache = SettingsCache::instance();
    cache.get().spellGroupsJson = serializeToJson();
    cache.markDirty();
}

} // namespace spell_grouping
