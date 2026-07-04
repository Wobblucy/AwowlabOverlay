#pragma once

#include "imgui.h"
#include "Color/ActorColorGenerator.h"
#include "Core/NumberFormatter.h"
#include "Core/UIUtils.h"
#include "Database/CombatDatabase.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <sstream>
#include <iomanip>

namespace awow { class ISpellIconRenderer; }

namespace ui {

// Special GUID for the virtual "Blacklist" actor that aggregates blacklisted damage
inline constexpr const char* BLACKLIST_ACTOR_GUID = "Blacklist-0-0-0-0";

// Shared request to open breakdown panel for an actor
// Used by both DPS and healing meters
struct BreakdownRequest {
    std::string actorGuid;
    ActorCombatStats stats;
    uint32_t duration_ms = 0;
};

// Configuration for meter bar rendering
struct MeterBarConfig {
    float bar_height = 20.0f;
    float bar_spacing = 1.0f;
    float right_margin = 160.0f;  // Space for stats text
    ImVec4 default_color{0.4f, 0.4f, 0.8f, 1.0f};  // Default blue
    bool blend_with_actor_color = true;  // Use actor color from colorGen
    ImVec4 color_blend{1.0f, 1.0f, 1.0f, 1.0f};  // Multiply factor for actor color

    // What to show
    bool show_percent = true;
    bool show_per_second = true;
    const char* per_second_label = "DPS";  // "DPS", "HPS", "DTPS"
    const char* per_second_tooltip_label = "DPS";

    // Healing-specific options
    bool show_overhealing = false;        // Show overheal % on bar and tooltip
    const char* amount_label = "Damage";  // "Damage" or "Healing" for tooltips

    // Interaction
    bool allow_expansion = true;  // Right-click to expand spell breakdown
    bool allow_selection = true;  // Left-click to select
};

// Result from rendering (for click handling)
struct MeterBarResult {
    std::string clicked_actor;      // GUID of left-clicked actor
    std::string toggled_actor;      // GUID of right-clicked actor (toggle expansion)
};

// Forward declare - implementation in .cpp to avoid circular dependency
ImTextureID getSpellIconSafe(awow::ISpellIconRenderer* iconLoader, uint32_t spellId);

// Renders a single meter bar with the given configuration
// Returns info about any clicked actors
// combatGuidToName: optional fallback map for pet/guardian names from combat events
MeterBarResult renderMeterBar(
    size_t index,
    const ActorCombatStats& stats,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    int64_t grandTotal,
    uint32_t duration_ms,
    float maxAmount,
    float availableWidth,
    const MeterBarConfig& config,
    bool isExpanded,
    awow::ISpellIconRenderer* iconLoader = nullptr,
    const std::unordered_map<std::string, std::string>* combatGuidToName = nullptr,
    const std::unordered_map<uint32_t, std::string>* spellNameFallback = nullptr
);

// Render spell breakdown bars (smaller, indented) with optional spell icons.
// spellNameFallback: overlay-mode name lookup used when SpellDataTable is
// not populated. Keyed by spell ID. Live combat logs already carry the
// spell name next to the spell ID, so we can display it in the client's
// locale without any spell binary loaded. Checked before SPELL_NAME so
// the fallback also wins for spell IDs the SpellDataTable is missing
// (e.g. brand-new patches).
void renderSpellBreakdown(
    const std::vector<SpellCombatStats>& spells,
    int64_t actorTotal,
    const ImVec4& barColor = ImVec4(0.5f, 0.5f, 0.6f, 0.8f),
    const char* zeroSpellLabel = "Melee",
    awow::ISpellIconRenderer* iconLoader = nullptr,
    const std::unordered_map<uint32_t, std::string>* spellNameFallback = nullptr
);

} // namespace ui
