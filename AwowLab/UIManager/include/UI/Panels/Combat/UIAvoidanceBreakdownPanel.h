#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <cstdint>
#include <imgui.h>
#include "Database/AvoidanceDatabase.h"

class ActorColorGenerator;
namespace awow { class ISpellIconRenderer; }

// Modal popup for the "what did this target avoid" drill-down.
// Opened from the Avoidance meter when a row is clicked. Shows:
//   - target name + total damage avoided in the window
//   - ranked list of enemies who attacked (with amount + count per enemy)
//   - ranked list of spells that were avoided (with per-miss-type split)
//
// Snapshot-based: we cache the AvoidanceBreakdown at open() time so
// the modal keeps rendering even after the DB rebuilds under it.
class UIAvoidanceBreakdownPanel {
public:
    UIAvoidanceBreakdownPanel() = default;

    // Open the modal for the given target with a pre-computed breakdown.
    // The AvoidanceBreakdown is copied into the panel so the caller can
    // free the DB afterwards; the modal stays open across DB refreshes.
    void open(const std::string& targetGuid,
              const std::string& targetName,
              const AvoidanceBreakdown& breakdown,
              uint32_t windowDuration_ms);

    void close();
    bool isOpen() const { return isOpen_; }

    // Size ImGui laid out for the modal on its last rendered frame.
    ImVec2 getLastMeasuredSize() const { return lastMeasuredSize_; }

    // Called every frame; no-ops when closed. spellNameFallback is the
    // same overlay-mode name lookup used by the meter panels.
    void render(
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName,
        const std::unordered_map<uint32_t, std::string>* spellNameFallback
    );

private:
    bool isOpen_ = false;
    bool shouldOpenPopup_ = false;

    ImVec2 lastMeasuredSize_ = ImVec2(0, 0);
    std::string targetGuid_;
    std::string targetName_;
    AvoidanceBreakdown breakdown_;
    uint32_t windowDuration_ms_ = 0;
};
