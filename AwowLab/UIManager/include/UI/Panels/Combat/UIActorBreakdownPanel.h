#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <cstdint>
#include <chrono>
#include <imgui.h>
#include "Database/CombatDatabase.h"
#include "UI/Panels/Combat/BreakdownGraphRenderer.h"
#include "UI/Panels/Combat/BreakdownSpellTableRenderer.h"

class ActorColorGenerator;
namespace awow { class ISpellIconRenderer; }

// Modal popup window for detailed actor combat breakdown
// Shows spell-by-spell damage breakdown with normal/crit statistics
class UIActorBreakdownPanel {
public:
    UIActorBreakdownPanel() = default;

    // Call to open the modal window for a specific actor
    // Shows full encounter data with time range selection
    // metricType determines whether to show damage (DamageDealt) or healing (HealingDone) breakdown
    void open(const std::string& actorGuid, CombatMetricType metricType);

    // Call to close the modal window
    void close();

    // Returns true if the modal is currently open
    bool isOpen() const { return isOpen_; }

    // Size ImGui laid out for the modal on its last rendered frame.
    // (0,0) if the modal has not rendered yet - callers should treat
    // that as "use a generous fallback."
    ImVec2 getLastMeasuredSize() const { return lastMeasuredSize_; }

    // Render the modal window (call every frame, handles open/close state internally)
    // Stats are refreshed from database every second for live updates.
    // spellNameFallback: overlay-mode name lookup keyed by spell ID.
    // Threaded to the table renderer and to the sidebar's spell-name
    // display so drill-downs work without SpellDataTable loaded.
    void render(const CombatDatabase* combatDb,
                const std::shared_ptr<ActorColorGenerator>& colorGen,
                const std::unordered_map<std::string_view, std::string_view>& guidToName,
                awow::ISpellIconRenderer* iconLoader,
                uint32_t currentTime_ms,
                bool useFullEncounter,
                const std::unordered_map<uint32_t, std::string>* spellNameFallback = nullptr);

private:
    bool isOpen_ = false;
    bool shouldOpenPopup_ = false;

    // Frames left to keep the window pinned to the viewport center.
    // The overlay OS window auto-grows over the first few frames after
    // opening, which moves the center; re-centering through that
    // stretch keeps the window centered on the final size, after which
    // the user can drag it wherever they like.
    int recenterFrames_ = 0;

    // Size of the modal window as ImGui actually laid it out on the
    // last frame it rendered. Used by OverlayApplication's
    // auto-grow so the outer OS window matches the modal's real
    // content-driven size instead of a hardcoded guess. Zero until
    // the modal has rendered at least once.
    ImVec2 lastMeasuredSize_ = ImVec2(0, 0);

    // Current actor and metric type being displayed
    std::string actorGuid_;
    CombatMetricType metricType_ = CombatMetricType::DamageDealt;
    ActorCombatStats actorStats_;
    uint32_t duration_ms_ = 0;
    uint32_t currentTime_ms_ = 0;  // For playback indicator

    // Selected spell index for detailed sidebar
    int selectedSpellIndex_ = -1;

    // Refresh timer (update stats on time range change)
    std::chrono::steady_clock::time_point lastRefresh_;
    static constexpr int REFRESH_INTERVAL_MS = 1000;
    bool needsImmediateRefresh_ = false;  // Force refresh on first render after open

    // Extracted components
    BreakdownGraphRenderer graphRenderer_;
    BreakdownSpellTableRenderer tableRenderer_;

    // Per-frame spell-name fallback (nullptr for the main app path).
    const std::unordered_map<uint32_t, std::string>* spellNameFallback_ = nullptr;

    // Resolved display names for the pet spell groups (pet guid -> name),
    // rebuilt each stats refresh and handed to the table renderer so pet
    // group headers show "[Lesser Ghoul]" rather than a raw guid.
    std::unordered_map<std::string, std::string> petGroupNames_;

    // Special actor GUID for blacklist view
    static constexpr const char* BLACKLIST_ACTOR_GUID = "__BLACKLISTED__";

    // Refresh stats from database
    void refreshStats(const CombatDatabase* combatDb, uint32_t currentTime_ms, bool useFullEncounter);

    // Render subsections (kept in main panel)
    void renderHeader(const std::unordered_map<std::string_view, std::string_view>& guidToName,
                      const std::shared_ptr<ActorColorGenerator>& colorGen);
    void renderSpellSidebar(awow::ISpellIconRenderer* iconLoader);
    void renderTargetsSection(const std::unordered_map<std::string_view, std::string_view>& guidToName);
    void renderPetsSection(const std::unordered_map<std::string_view, std::string_view>& guidToName);
};
