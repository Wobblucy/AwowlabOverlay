#include "PhaseSettings.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <charconv>

namespace {

const char* kindName(PhaseSettings::RuleKind kind) {
    switch (kind) {
        case PhaseSettings::RuleKind::Elapsed: return "time";
        case PhaseSettings::RuleKind::Emote:   return "emote";
        case PhaseSettings::RuleKind::SpellCast:
        default:                               return "cast";
    }
}

PhaseSettings::RuleKind kindFromName(std::string_view name) {
    if (name == "time")  return PhaseSettings::RuleKind::Elapsed;
    if (name == "emote") return PhaseSettings::RuleKind::Emote;
    return PhaseSettings::RuleKind::SpellCast;
}

} // namespace

PhaseSettings& PhaseSettings::instance() {
    static PhaseSettings settings;
    return settings;
}

const std::vector<PhaseSettings::PhaseRule>* PhaseSettings::rulesFor(uint32_t encounterId) const {
    auto it = rules.find(encounterId);
    return (it != rules.end() && !it->second.empty()) ? &it->second : nullptr;
}

void PhaseSettings::addRule(uint32_t encounterId, const PhaseRule& rule) {
    if (encounterId == 0) return;
    if (rule.kind == RuleKind::SpellCast && rule.spellId == 0) return;
    if (rule.kind == RuleKind::Elapsed && rule.elapsedMs <= 0) return;
    if (rule.kind == RuleKind::Emote && rule.emoteText.empty()) return;

    auto& encounterRules = rules[encounterId];
    for (const auto& existing : encounterRules) {
        if (existing.matches(rule)) {
            return;
        }
    }
    encounterRules.push_back(rule);
}

void PhaseSettings::addRule(uint32_t encounterId, uint32_t spellId, const std::string& label) {
    PhaseRule rule;
    rule.kind = RuleKind::SpellCast;
    rule.spellId = spellId;
    rule.label = label;
    addRule(encounterId, rule);
}

void PhaseSettings::removeRule(uint32_t encounterId, uint32_t spellId) {
    PhaseRule rule;
    rule.kind = RuleKind::SpellCast;
    rule.spellId = spellId;
    removeRule(encounterId, rule);
}

void PhaseSettings::removeRule(uint32_t encounterId, const PhaseRule& rule) {
    auto it = rules.find(encounterId);
    if (it == rules.end()) {
        return;
    }
    auto& encounterRules = it->second;
    encounterRules.erase(
        std::remove_if(encounterRules.begin(), encounterRules.end(),
                       [&rule](const PhaseRule& r) { return r.matches(rule); }),
        encounterRules.end());
    if (encounterRules.empty()) {
        rules.erase(it);
    }
}

bool PhaseSettings::hasRule(uint32_t encounterId, uint32_t spellId) const {
    auto it = rules.find(encounterId);
    if (it == rules.end()) {
        return false;
    }
    for (const auto& rule : it->second) {
        if (rule.kind == RuleKind::SpellCast && rule.spellId == spellId) {
            return true;
        }
    }
    return false;
}

std::string PhaseSettings::toJson() const {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    for (const auto& [encounterId, encounterRules] : rules) {
        if (encounterRules.empty()) continue;

        rapidjson::Value ruleArray(rapidjson::kArrayType);
        for (const auto& rule : encounterRules) {
            rapidjson::Value ruleObj(rapidjson::kObjectType);
            ruleObj.AddMember("kind", rapidjson::Value(kindName(rule.kind), alloc), alloc);
            switch (rule.kind) {
                case RuleKind::SpellCast:
                    ruleObj.AddMember("spell", rule.spellId, alloc);
                    break;
                case RuleKind::Elapsed:
                    ruleObj.AddMember("ms", rule.elapsedMs, alloc);
                    break;
                case RuleKind::Emote:
                    ruleObj.AddMember("text", rapidjson::Value(rule.emoteText.c_str(), alloc), alloc);
                    break;
            }
            if (!rule.label.empty()) {
                ruleObj.AddMember("label", rapidjson::Value(rule.label.c_str(), alloc), alloc);
            }
            ruleArray.PushBack(ruleObj, alloc);
        }

        std::string key = std::to_string(encounterId);
        doc.AddMember(rapidjson::Value(key.c_str(), alloc), ruleArray, alloc);
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    return buffer.GetString();
}

void PhaseSettings::fromJson(const std::string& json) {
    rules.clear();

    if (json.empty()) {
        return;
    }

    rapidjson::Document doc;
    if (doc.Parse(json.c_str()).HasParseError() || !doc.IsObject()) {
        return;
    }

    for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
        if (!it->value.IsArray()) continue;

        uint32_t encounterId = 0;
        std::string_view key(it->name.GetString(), it->name.GetStringLength());
        std::from_chars(key.data(), key.data() + key.size(), encounterId);
        if (encounterId == 0) continue;

        std::vector<PhaseRule> encounterRules;
        for (const auto& ruleVal : it->value.GetArray()) {
            if (!ruleVal.IsObject()) continue;

            PhaseRule rule;
            // Rules saved before rule kinds existed carry only "spell"
            if (ruleVal.HasMember("kind") && ruleVal["kind"].IsString()) {
                rule.kind = kindFromName(ruleVal["kind"].GetString());
            }
            if (ruleVal.HasMember("spell") && ruleVal["spell"].IsUint()) {
                rule.spellId = ruleVal["spell"].GetUint();
            }
            if (ruleVal.HasMember("ms") && ruleVal["ms"].IsInt()) {
                rule.elapsedMs = ruleVal["ms"].GetInt();
            }
            if (ruleVal.HasMember("text") && ruleVal["text"].IsString()) {
                rule.emoteText = ruleVal["text"].GetString();
            }
            if (ruleVal.HasMember("label") && ruleVal["label"].IsString()) {
                rule.label = ruleVal["label"].GetString();
            }

            bool valid = (rule.kind == RuleKind::SpellCast && rule.spellId != 0) ||
                         (rule.kind == RuleKind::Elapsed && rule.elapsedMs > 0) ||
                         (rule.kind == RuleKind::Emote && !rule.emoteText.empty());
            if (valid) {
                encounterRules.push_back(std::move(rule));
            }
        }
        if (!encounterRules.empty()) {
            rules[encounterId] = std::move(encounterRules);
        }
    }
}
