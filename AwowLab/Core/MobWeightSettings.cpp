#include "MobWeightSettings.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <charconv>

MobWeightSettings& MobWeightSettings::instance() {
    static MobWeightSettings settings;
    return settings;
}

float MobWeightSettings::weightFor(uint32_t npcId) const {
    if (!enabled || npcId == 0) {
        return 1.0f;
    }
    auto it = weights.find(npcId);
    return (it != weights.end()) ? it->second : 1.0f;
}

uint32_t MobWeightSettings::npcIdFromGuid(std::string_view guid) {
    // Unit GUID layout: Type-0-serverId-instanceId-zoneUid-npcId-spawnUid.
    // Players ("Player-realm-hex") have no npc id.
    if (guid.starts_with("Player-") || guid.empty()) {
        return 0;
    }

    // Walk to the 6th dash-separated field
    size_t pos = 0;
    for (int field = 0; field < 5; ++field) {
        pos = guid.find('-', pos);
        if (pos == std::string_view::npos) {
            return 0;
        }
        ++pos;
    }
    size_t end = guid.find('-', pos);
    if (end == std::string_view::npos) {
        end = guid.size();
    }

    uint32_t npcId = 0;
    std::from_chars(guid.data() + pos, guid.data() + end, npcId);
    return npcId;
}

std::string MobWeightSettings::toJson() const {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    doc.AddMember("enabled", enabled, alloc);

    rapidjson::Value weightsObj(rapidjson::kObjectType);
    for (const auto& [npcId, weight] : weights) {
        std::string key = std::to_string(npcId);
        weightsObj.AddMember(
            rapidjson::Value(key.c_str(), alloc),
            rapidjson::Value(weight),
            alloc);
    }
    doc.AddMember("weights", weightsObj, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    return buffer.GetString();
}

void MobWeightSettings::fromJson(const std::string& json) {
    weights.clear();
    enabled = true;

    if (json.empty()) {
        return;
    }

    rapidjson::Document doc;
    if (doc.Parse(json.c_str()).HasParseError() || !doc.IsObject()) {
        return;
    }

    if (doc.HasMember("enabled") && doc["enabled"].IsBool()) {
        enabled = doc["enabled"].GetBool();
    }

    if (doc.HasMember("weights") && doc["weights"].IsObject()) {
        for (auto it = doc["weights"].MemberBegin(); it != doc["weights"].MemberEnd(); ++it) {
            if (!it->value.IsNumber()) continue;

            uint32_t npcId = 0;
            std::string_view key(it->name.GetString(), it->name.GetStringLength());
            std::from_chars(key.data(), key.data() + key.size(), npcId);
            if (npcId == 0) continue;

            float weight = std::clamp(it->value.GetFloat(), 0.0f, 1.0f);
            if (weight < 0.9995f) {
                weights[npcId] = weight;
            }
        }
    }
}
