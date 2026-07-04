#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <imgui.h>

// Forward declarations
namespace RaidNotes {
    class RaidNoteDocument;
}

/**
 * Popup context menu shown when right-clicking on spell icons.
 * Provides options for: Wowhead lookup, spell settings navigation, raid notes.
 *
 * Static singleton pattern - only one popup can be open at a time.
 */
class SpellContextMenu {
public:
    /**
     * Check if the last rendered item should show the context menu.
     * Call this after rendering a spell icon.
     * @param actorGuid Optional actor GUID for spec lookup when opening spell settings
     * @return true if context menu was opened
     */
    static bool checkAndShow(uint32_t spellId, const std::string& spellName,
                             int32_t timestamp_ms, bool isHostile,
                             const std::vector<int32_t>& groupedTimestamps = {},
                             std::string_view actorGuid = {});

    /**
     * Check if a specific rect should show the context menu.
     * For use with ImDrawList-rendered icons.
     * @param actorGuid Optional actor GUID for spec lookup when opening spell settings
     */
    static bool checkAndShowRect(uint32_t spellId, const std::string& spellName,
                                  int32_t timestamp_ms, bool isHostile,
                                  ImVec2 min, ImVec2 max,
                                  const std::vector<int32_t>& groupedTimestamps = {},
                                  std::string_view actorGuid = {});

    /**
     * Render the popup menu. Call once per frame from UIWindowManager.
     */
    static void renderPopup();

    /**
     * Check if popup is currently open.
     */
    static bool isPopupOpen() { return s_popupOpen; }

    /**
     * Navigation request for opening settings panels.
     */
    struct NavigationRequest {
        enum class Target { None, SpellSettings, AutoAnnotations };
        Target target = Target::None;
        uint32_t spellId = 0;
        bool isHostile = false;  // For smart routing
        std::string actorGuid;   // For spec lookup in spell settings
    };

    /**
     * Consume pending navigation request (returns and clears).
     */
    static NavigationRequest consumePendingNavigation();

    /**
     * Check if there's a pending raid note request.
     */
    static bool hasPendingRaidNoteRequest();

    /**
     * Consume the raid note request (triggers RaidNoteContextMenu).
     */
    static void consumeRaidNoteRequest();

    /**
     * Set the active encounter id. The menu itself has no encounter
     * context, but needs it to decide between "set phase trigger" and
     * "remove phase trigger". UIWindowManager calls this when an
     * encounter loads.
     */
    static void setCurrentEncounterId(uint32_t encounterId) { s_currentEncounterId = encounterId; }

    /**
     * Phase trigger request: mark or unmark a boss spell as a phase
     * boundary for the current encounter.
     */
    struct PhaseRequest {
        uint32_t spellId = 0;
        bool add = true;  // false = remove the existing trigger
    };

    /**
     * Check if there's a pending phase trigger request.
     */
    static bool hasPendingPhaseRequest();

    /**
     * Consume the phase trigger request (returns and clears).
     */
    static PhaseRequest consumePhaseRequest();

    /**
     * Get pending context data for raid notes.
     */
    static uint32_t getPendingSpellId() { return s_spellId; }
    static const std::string& getPendingSpellName() { return s_spellName; }
    static int32_t getPendingTimestampMs() { return s_timestampMs; }
    static bool getPendingIsHostile() { return s_isHostile; }
    static const std::vector<int32_t>& getPendingGroupedTimestamps() { return s_groupedTimestamps; }

private:
    // Popup state
    static bool s_popupOpen;
    static bool s_popupJustOpened;
    static ImVec2 s_popupPos;

    // Context for the current popup
    static uint32_t s_spellId;
    static std::string s_spellName;
    static int32_t s_timestampMs;
    static bool s_isHostile;
    static std::vector<int32_t> s_groupedTimestamps;
    static std::string s_actorGuid;

    // Pending requests (set when user selects an option)
    static NavigationRequest s_pendingNavigation;
    static bool s_pendingRaidNoteRequest;

    // Phase trigger integration
    static uint32_t s_currentEncounterId;
    static PhaseRequest s_pendingPhaseRequest;
    static bool s_hasPendingPhaseRequest;
};
