#include "ColorUtils.h"

namespace awow {

::ActorColor hsvToRgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r, g, b;
    if (h < 1.0f / 6.0f) {
        r = c; g = x; b = 0;
    } else if (h < 2.0f / 6.0f) {
        r = x; g = c; b = 0;
    } else if (h < 3.0f / 6.0f) {
        r = 0; g = c; b = x;
    } else if (h < 4.0f / 6.0f) {
        r = 0; g = x; b = c;
    } else if (h < 5.0f / 6.0f) {
        r = x; g = 0; b = c;
    } else {
        r = c; g = 0; b = x;
    }

    return ::ActorColor{r + m, g + m, b + m};
}

uint32_t parseSpawnIndex(std::string_view guid) {
    size_t lastDash = guid.rfind('-');
    if (lastDash == std::string_view::npos || lastDash + 1 >= guid.length()) {
        return 0;
    }

    std::string_view spawnUIDPrefix = guid.substr(lastDash + 1, 5);
    if (spawnUIDPrefix.length() < 5) {
        return 0;
    }

    uint32_t value = 0;
    for (char c : spawnUIDPrefix) {
        value *= 16;
        if (c >= '0' && c <= '9') {
            value += (c - '0');
        } else if (c >= 'A' && c <= 'F') {
            value += (c - 'A' + 10);
        } else if (c >= 'a' && c <= 'f') {
            value += (c - 'a' + 10);
        }
    }

    return (value & 0xffff8) >> 3;
}

::ActorColor generateActorColor(::ActorType type, const ::UnitFlags& flags, std::string_view guid) {
    float baseHue = 0.0f;
    float saturation = 0.7f;
    float value = 0.9f;
    uint32_t guidHash = static_cast<uint32_t>(std::hash<std::string_view>{}(guid));

    switch (type) {
        case ::ActorType::Creature: {
            uint32_t spawnIndex = parseSpawnIndex(guid);
            uint32_t colorSeed = (spawnIndex * 2654435761U) ^ guidHash;

            if (flags.isHostile()) {
                baseHue = (colorSeed % 40) / 360.0f;
                saturation = 0.75f + ((colorSeed % 20) / 100.0f);
                value = 0.85f + ((colorSeed % 15) / 100.0f);
            } else if (flags.isFriendly()) {
                baseHue = 0.28f + (colorSeed % 60) / 360.0f;
                saturation = 0.6f + ((colorSeed % 20) / 100.0f);
                value = 0.7f + ((colorSeed % 20) / 100.0f);
            } else {
                baseHue = 0.14f + (colorSeed % 30) / 360.0f;
                saturation = 0.5f + ((colorSeed % 20) / 100.0f);
                value = 0.8f + ((colorSeed % 15) / 100.0f);
            }
            break;
        }

        case ::ActorType::Vehicle: {
            uint32_t spawnIndex = parseSpawnIndex(guid);
            uint32_t colorSeed = (spawnIndex * 2654435761U) ^ guidHash;
            baseHue = 0.17f + (colorSeed % 60) / 360.0f;
            saturation = 0.6f + ((colorSeed % 20) / 100.0f);
            value = 0.8f + ((colorSeed % 20) / 100.0f);
            break;
        }

        case ::ActorType::Pet:
            if (flags.isFriendly()) {
                baseHue = 0.75f + (guidHash % 60) / 360.0f;
                saturation = 0.65f;
                value = 0.8f;
            } else {
                baseHue = (guidHash % 20) / 360.0f;
                saturation = 0.7f;
                value = 0.6f;
            }
            break;

        case ::ActorType::Player:
            // Players without spec info - fallback color
            if (flags.isFriendly()) {
                baseHue = 0.5f + (guidHash % 60) / 360.0f;
                saturation = 0.8f;
                value = 1.0f;
            } else {
                baseHue = (guidHash % 30) / 360.0f;
                saturation = 0.9f;
                value = 1.0f;
            }
            break;

        default:
            return ::ActorColor{0.5f, 0.5f, 0.5f};
    }

    return hsvToRgb(baseHue, saturation, value);
}

::ActorType getActorTypeFromGuid(std::string_view guid) {
    if (guid.size() >= 7 && guid.substr(0, 7) == "Player-") return ::ActorType::Player;
    if (guid.size() >= 9 && guid.substr(0, 9) == "Creature-") return ::ActorType::Creature;
    if (guid.size() >= 8 && guid.substr(0, 8) == "Vehicle-") return ::ActorType::Vehicle;
    if (guid.size() >= 4 && guid.substr(0, 4) == "Pet-") return ::ActorType::Pet;
    return ::ActorType::Unknown;
}

} // namespace awow
