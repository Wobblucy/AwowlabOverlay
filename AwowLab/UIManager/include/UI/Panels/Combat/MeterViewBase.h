#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include "CombatDatabase.h"
#include "DispelDatabase.h"
#include "Core/MeterBarRenderer.h"
#include "Core/UIUtils.h"
#include "UI/SpellGroupSettings.h"
#include "imgui.h"

class ActorColorGenerator;
namespace awow { class ISpellIconRenderer; }

// Shared rendering utilities for meter views
// These functions consolidate the duplicated bar chart rendering logic
// that was previously spread across UIMeterPanel's private methods.
namespace meter_rendering {

// Shared throttling logic - returns true if stats refresh is needed
// Handles time jumps (scrubbing), mode switches, and periodic refresh
inline bool needsRefresh(
    uint32_t currentTime_ms,
    uint32_t lastPlaybackTime_ms,
    uint32_t lastRefreshTime_ms,
    bool viewTypeMismatch,
    bool timeModeSwitched,
    bool cacheEmpty,
    float refreshInterval_ms = 1000.0f
) {
    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);

    // Detect time scrubbing (large jump in playback time)
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms + 500) ||
                        (lastPlaybackTime_ms > currentTime_ms + 500);

    return cacheEmpty
        || viewTypeMismatch
        || timeScrubbed
        || timeModeSwitched
        || (now_ms - lastRefreshTime_ms >= static_cast<uint32_t>(refreshInterval_ms));
}

// Render a combat bar chart with actor stats
// This renders the list of actors with their DPS/HPS bars, icons, and values
void renderCombatBarChart(
    const std::vector<ActorCombatStats>& stats,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    int64_t grandTotal,
    uint32_t duration_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    const ImVec4& barColor,
    int instanceId,
    std::optional<std::string>& clickedActor,
    std::optional<ui::BreakdownRequest>& breakdownRequest,
    std::string& expandedActorGuid
);

// Render a dispel/interrupt bar chart
void renderDispelBarChart(
    const std::vector<ActorDispelStats>& stats,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t grandTotal,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    int instanceId
);

// Render total summary line (e.g., "Total: 1.2M | 45.6K/s")
void renderTotalSummary(int64_t grandTotal, float grandAmountPerSecond);

// Render dispel/interrupt summary line
void renderDispelSummary(uint32_t dispelCount, uint32_t interruptCount);

} // namespace meter_rendering
