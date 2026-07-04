#include "Color/ActorColorGenerator.h"
#include <cmath>
#include <functional>

ActorColor ActorColorGenerator::getColor(std::string_view guid, [[maybe_unused]] ActorType type, [[maybe_unused]] const UnitFlags& flags) {
    // This method is no longer used for color generation - colors are pre-computed
    // during extraction. This is kept for backwards compatibility but just returns
    // the cached color or gray.
    auto it = colorCache_.find(guid);
    if (it != colorCache_.end()) {
        return it->second;
    }
    return ActorColor{0.5f, 0.5f, 0.5f}; // Gray fallback
}

ActorColor ActorColorGenerator::getCachedColor(std::string_view guid) const {
    auto it = colorCache_.find(guid);
    if (it != colorCache_.end()) {
        return it->second;
    }
    return ActorColor{0.5f, 0.5f, 0.5f}; // Gray for unknown
}

bool ActorColorGenerator::hasColor(std::string_view guid) const {
    return colorCache_.find(guid) != colorCache_.end();
}

void ActorColorGenerator::clear() {
    colorCache_.clear();
    actorTypes_.clear();
    actorFlags_.clear();
    specIdCache_.clear();
    nameCache_.clear();
    nonFriendlyNameToGuids_.clear();
}

void ActorColorGenerator::cacheActorInfo(std::string_view guid, ActorType type, const UnitFlags& flags) {
    actorTypes_[guid] = type;
    actorFlags_[guid] = flags;
}

void ActorColorGenerator::cacheColor(std::string_view guid, const ActorColor& color) {
    colorCache_[guid] = color;
}

const ActorType* ActorColorGenerator::getActorType(std::string_view guid) const {
    auto it = actorTypes_.find(guid);
    if (it != actorTypes_.end()) {
        return &it->second;
    }
    return nullptr;
}

const UnitFlags* ActorColorGenerator::getActorFlags(std::string_view guid) const {
    auto it = actorFlags_.find(guid);
    if (it != actorFlags_.end()) {
        return &it->second;
    }
    return nullptr;
}

void ActorColorGenerator::cacheSpecId(std::string_view guid, uint16_t specId) {
    if (specId > 0) {
        specIdCache_[guid] = specId;
    }
}

uint16_t ActorColorGenerator::getSpecId(std::string_view guid) const {
    auto it = specIdCache_.find(guid);
    if (it != specIdCache_.end()) {
        return it->second;
    }
    return 0;
}

void ActorColorGenerator::cacheActorName(std::string_view guid, std::string_view name) {
    if (!name.empty()) {
        nameCache_[guid] = name;
    }
}

std::string_view ActorColorGenerator::getActorName(std::string_view guid) const {
    auto it = nameCache_.find(guid);
    if (it != nameCache_.end()) {
        return it->second;
    }
    return std::string_view{};
}

void ActorColorGenerator::cacheNonFriendlyActor(std::string_view guid, std::string_view name, const UnitFlags& flags) {
    // Only cache hostile or neutral actors
    if (!flags.isHostile() && !flags.isNeutral()) {
        return;
    }

    // Check if this GUID is already in the list for this name
    auto& guids = nonFriendlyNameToGuids_[name];
    for (const auto& existingGuid : guids) {
        if (existingGuid == guid) {
            return;  // Already cached
        }
    }
    guids.push_back(guid);
}

const std::vector<std::string_view>* ActorColorGenerator::getGuidsForName(std::string_view name) const {
    auto it = nonFriendlyNameToGuids_.find(name);
    if (it != nonFriendlyNameToGuids_.end()) {
        return &it->second;
    }
    return nullptr;
}

ActorColor ActorColorGenerator::getColorForName(std::string_view name) const {
    // Hash the name string for deterministic color
    size_t hash = std::hash<std::string_view>{}(name);

    // Generate HSV color in hostile/neutral range (reds, oranges, yellows)
    float hue = static_cast<float>(hash % 60) / 360.0f;  // 0-60 degrees (red to yellow)
    float sat = 0.6f + static_cast<float>((hash >> 8) % 30) / 100.0f;  // 60-90%
    float val = 0.7f + static_cast<float>((hash >> 16) % 20) / 100.0f; // 70-90%

    // HSV to RGB conversion
    float c = val * sat;
    float x = c * (1.0f - std::fabs(std::fmod(hue * 6.0f, 2.0f) - 1.0f));
    float m = val - c;

    float r, g, b;
    int sector = static_cast<int>(hue * 6.0f);
    switch (sector) {
        case 0: r = c; g = x; b = 0; break;
        case 1: r = x; g = c; b = 0; break;
        case 2: r = 0; g = c; b = x; break;
        case 3: r = 0; g = x; b = c; break;
        case 4: r = x; g = 0; b = c; break;
        default: r = c; g = 0; b = x; break;
    }

    return ActorColor{r + m, g + m, b + m};
}
