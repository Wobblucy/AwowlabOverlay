#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <memory>
#include "../Color/ActorColorGenerator.h"
#include "../../Database/CombatDatabase.h"
#include "Core/LocalizationManager.h"

namespace ui {

// ============================================================================
// Environment (world) source detection
// ============================================================================

// WoW attributes environmental damage - falling, lava, fatigue, drowning,
// fire - to a synthetic source with the null GUID and the literal name
// "nil". Detect either form so meters can show a friendly "Environment"
// label instead of a raw "nil" row.
inline bool isEnvironmentSource(const std::string& guid, const std::string& resolvedName) {
    if (resolvedName == "nil") {
        return true;
    }
    // Null GUID: all zeros, optionally with a "Creature-0-0-0-0-0-0-..." shape.
    if (guid == "0000000000000000") {
        return true;
    }
    return false;
}

// Localized display label for the environmental damage source.
inline std::string environmentName() {
    return std::string(L("meter.environment"));
}

// ============================================================================
// UTF-8 String Utilities
// ============================================================================

// Count UTF-8 characters (code points), not bytes
// Handles ASCII and multi-byte sequences (2-4 bytes for CJK, etc.)
inline size_t utf8CharCount(const std::string& str) {
    size_t count = 0;
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if ((c & 0x80) == 0) i += 1;           // ASCII (0xxxxxxx)
        else if ((c & 0xE0) == 0xC0) i += 2;   // 2-byte (110xxxxx)
        else if ((c & 0xF0) == 0xE0) i += 3;   // 3-byte (1110xxxx) - CJK
        else if ((c & 0xF8) == 0xF0) i += 4;   // 4-byte (11110xxx)
        else i += 1;  // Invalid sequence, skip byte
        count++;
    }
    return count;
}

// Truncate string at UTF-8 character boundaries (not byte boundaries)
// Returns at most maxChars complete characters
inline std::string utf8Truncate(const std::string& str, size_t maxChars) {
    size_t count = 0;
    size_t bytePos = 0;
    while (bytePos < str.size() && count < maxChars) {
        unsigned char c = static_cast<unsigned char>(str[bytePos]);
        size_t charLen = 1;
        if ((c & 0x80) == 0) charLen = 1;
        else if ((c & 0xE0) == 0xC0) charLen = 2;
        else if ((c & 0xF0) == 0xE0) charLen = 3;
        else if ((c & 0xF8) == 0xF0) charLen = 4;

        // Don't go past end of string
        if (bytePos + charLen > str.size()) break;

        bytePos += charLen;
        count++;
    }
    return str.substr(0, bytePos);
}

// ============================================================================
// Actor Utilities
// ============================================================================

// Check if an actor is friendly based on flags or GUID fallback
inline bool isActorFriendly(
    const std::string& actor_guid,
    const std::shared_ptr<ActorColorGenerator>& colorGen
) {
    if (colorGen) {
        const UnitFlags* flags = colorGen->getActorFlags(actor_guid);
        if (flags) {
            return (flags->reaction == UnitReaction::Friendly);
        }
    }
    // Fallback: Player GUIDs are typically friendly
    return actor_guid.starts_with("Player-");
}

// Filter stats to only include friendly actors
// Returns filtered vector with at most maxResults entries
inline std::vector<ActorCombatStats> filterFriendlyActors(
    std::vector<ActorCombatStats> stats,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    size_t maxResults = 40,
    bool excludePets = false
) {
    if (!colorGen) {
        if (stats.size() > maxResults) {
            stats.resize(maxResults);
        }
        return stats;
    }

    std::vector<ActorCombatStats> filteredStats;
    filteredStats.reserve(stats.size());

    for (const auto& s : stats) {
        // Damage-taken keeps each actor's own hits (pets aren't merged into
        // their owner there), so a friendly pet would otherwise show as its
        // own row. excludePets keeps the list to actual players - a pet's
        // damage taken is its own, not the player's.
        if (excludePets && !s.actor_guid.starts_with("Player-")) {
            continue;
        }
        if (isActorFriendly(s.actor_guid, colorGen)) {
            filteredStats.push_back(s);
        }
        if (filteredStats.size() >= maxResults) break;
    }

    return filteredStats;
}

// Filter stats by reaction type (friendly or hostile)
// Returns filtered vector with at most maxResults entries
inline std::vector<ActorCombatStats> filterActorsByReaction(
    std::vector<ActorCombatStats> stats,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    bool wantFriendly,
    size_t maxResults = 40
) {
    if (!colorGen) {
        if (stats.size() > maxResults) {
            stats.resize(maxResults);
        }
        return stats;
    }

    std::vector<ActorCombatStats> filteredStats;
    filteredStats.reserve(stats.size());

    for (const auto& s : stats) {
        bool isFriendly = isActorFriendly(s.actor_guid, colorGen);
        if (isFriendly == wantFriendly) {
            filteredStats.push_back(s);
        }
        if (filteredStats.size() >= maxResults) break;
    }

    return filteredStats;
}

// Resolve actor GUID to display name with truncation
// Falls back to last part of GUID if name not found
// Optional combatGuidToName map provides names from combat events (for pets/guardians)
inline std::string resolveActorName(
    const std::string& guid,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    size_t maxLength = 12,
    const std::unordered_map<std::string, std::string>* combatGuidToName = nullptr
) {
    std::string displayName;

    // First try the position-based guidToName map (from frame cache)
    // Use explicit string_view to avoid iterator compatibility issues
    std::string_view guidView(guid);
    auto nameIt = guidToName.find(guidView);
    if (nameIt != guidToName.end()) {
        displayName = std::string(nameIt->second);
    } else if (combatGuidToName) {
        // Fallback to combat-derived GUID→Name map (for pets/guardians without position events)
        auto combatIt = combatGuidToName->find(guid);
        if (combatIt != combatGuidToName->end()) {
            displayName = combatIt->second;
        }
    }

    // Environmental damage (falling, lava, fatigue, ...) resolves to the
    // literal "nil" or carries the null GUID. Show a friendly localized
    // label rather than the raw log token.
    if (isEnvironmentSource(guid, displayName)) {
        return environmentName();
    }

    // Final fallback: use last part of GUID
    if (displayName.empty()) {
        size_t lastDash = guid.rfind('-');
        if (lastDash != std::string::npos) {
            displayName = guid.substr(lastDash + 1, 8);
        } else {
            displayName = guid.substr(0, 8);
        }
    }

    // Truncate if too long (UTF-8 safe - counts characters, not bytes)
    if (utf8CharCount(displayName) > maxLength) {
        displayName = utf8Truncate(displayName, maxLength - 1) + "..";
    }

    return displayName;
}

// Time window calculation result
struct TimeWindow {
    uint32_t startTime;
    uint32_t endTime;
    uint32_t duration_ms;
};

// Calculate clamped time window for database queries
inline TimeWindow calculateTimeWindow(
    uint32_t currentTime,
    uint32_t dbMinTime,
    uint32_t dbMaxTime,
    bool useFullEncounter
) {
    TimeWindow window;
    window.startTime = dbMinTime;

    if (useFullEncounter) {
        window.endTime = dbMaxTime;
    } else {
        window.endTime = currentTime;
        // Clamp to database bounds
        if (window.endTime < dbMinTime) {
            window.endTime = dbMinTime;
        }
        if (window.endTime > dbMaxTime) {
            window.endTime = dbMaxTime;
        }
    }

    window.duration_ms = window.endTime - window.startTime;
    if (window.duration_ms < 1) {
        window.duration_ms = 1;
    }

    return window;
}

} // namespace ui
