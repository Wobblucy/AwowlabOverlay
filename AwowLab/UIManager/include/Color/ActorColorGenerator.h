#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "structures.h"  // For UnitFlags
#include "../../Database/EventTypes.h"  // For ActorType, ActorColor

class ActorColorGenerator {
public:
    ActorColorGenerator() = default;

    // Get or generate a consistent color for an actor
    // Uses ActorType for geometry-based coloring, UnitFlags for reaction-based tinting
    ActorColor getColor(std::string_view guid, ActorType type, const UnitFlags& flags);

    // Get color from cache only (returns gray if not found)
    ActorColor getCachedColor(std::string_view guid) const;

    // Check if a color is cached for this GUID
    bool hasColor(std::string_view guid) const;

    // Clear the color cache
    void clear();

    // Get the actor type cache (GUID-based)
    const std::unordered_map<std::string_view, ActorType>& getActorTypes() const { return actorTypes_; }

    // Get the actor flags cache (combat log flags)
    const std::unordered_map<std::string_view, UnitFlags>& getActorFlags() const { return actorFlags_; }

    // Cache actor type and flags
    void cacheActorInfo(std::string_view guid, ActorType type, const UnitFlags& flags);

    // Cache pre-computed color (from ActorEvent.color)
    void cacheColor(std::string_view guid, const ActorColor& color);

    // Get cached actor type (returns nullptr if not found)
    const ActorType* getActorType(std::string_view guid) const;

    // Get cached actor flags (returns nullptr if not found)
    const UnitFlags* getActorFlags(std::string_view guid) const;

    // Cache spec ID for role detection (from COMBATANT_INFO)
    void cacheSpecId(std::string_view guid, uint16_t specId);

    // Get cached spec ID (returns 0 if not found)
    uint16_t getSpecId(std::string_view guid) const;

    // Cache actor name for name group resolution
    void cacheActorName(std::string_view guid, std::string_view name);

    // Get cached actor name (returns empty string_view if not found)
    std::string_view getActorName(std::string_view guid) const;

    // Track hostile/neutral actors grouped by name (for Enemies tab)
    void cacheNonFriendlyActor(std::string_view guid, std::string_view name, const UnitFlags& flags);

    // Get all GUIDs for a given actor name (returns nullptr if not found)
    const std::vector<std::string_view>* getGuidsForName(std::string_view name) const;

    // Get all non-friendly actor name groups (name -> list of GUIDs)
    const std::unordered_map<std::string_view, std::vector<std::string_view>>& getNonFriendlyNameGroups() const {
        return nonFriendlyNameToGuids_;
    }

    // Generate deterministic color from actor name hash
    ActorColor getColorForName(std::string_view name) const;

private:
    std::unordered_map<std::string_view, ActorColor> colorCache_;
    std::unordered_map<std::string_view, ActorType> actorTypes_;
    std::unordered_map<std::string_view, UnitFlags> actorFlags_;
    std::unordered_map<std::string_view, uint16_t> specIdCache_;
    std::unordered_map<std::string_view, std::string_view> nameCache_;  // GUID -> name

    // Non-friendly (hostile/neutral) actors grouped by name
    std::unordered_map<std::string_view, std::vector<std::string_view>> nonFriendlyNameToGuids_;
};
