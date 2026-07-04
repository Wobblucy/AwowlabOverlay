#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <cstdint>
#include <vector>
#include <imgui.h>
#include "Database/CombatDatabase.h"
#include "Database/DeathDatabase.h"
#include "Database/ResourceDatabase.h"

class ActorColorGenerator;
namespace awow { class ISpellIconRenderer; }

// Modal popup for post-mortem analysis.
//
// Left pane lists every death recorded for the selected actor with its
// timestamp and killing ability. Clicking one loads the right pane
// with:
//   - HP timeline chart (30 seconds before death)
//   - Filter slider that hides small hits
//   - Event table with spell names, amounts, and a per-row HP bar
//
// Snapshot-based: on open() we cache the DeathEvent list plus any
// data we need out of the databases so the modal keeps rendering even
// if the databases rebuild under it.
class UIDeathRecapPanel {
public:
    UIDeathRecapPanel() = default;

    // Open the modal for the given actor with every death of theirs.
    // `deaths` should already be sorted by timestamp (any order); the
    // panel will render them in chronological order.
    void open(const std::string& actorGuid,
              const std::string& actorName,
              std::vector<DeathEvent> deaths);

    void close();
    bool isOpen() const { return isOpen_; }

    ImVec2 getLastMeasuredSize() const { return lastMeasuredSize_; }

    // Called every frame; no-ops when closed. combatDb + resourceDb
    // are queried once when the user picks a death on the left pane
    // so the modal keeps working across data refreshes.
    void render(
        const CombatDatabase* combatDb,
        const ResourceDatabase* resourceDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName,
        const std::unordered_map<uint32_t, std::string>* spellNameFallback
    );

private:
    bool isOpen_ = false;
    bool shouldOpenPopup_ = false;
    // Keep centered while the overlay auto-grows after open (see
    // UIActorBreakdownPanel::recenterFrames_ for the full story)
    int recenterFrames_ = 0;
    ImVec2 lastMeasuredSize_ = ImVec2(0, 0);

    // Actor context (constant for the lifetime of the modal).
    std::string actorGuid_;
    std::string actorName_;
    std::vector<DeathEvent> deaths_;

    // Currently selected death index in deaths_ (SIZE_MAX = none).
    size_t selectedDeathIndex_ = SIZE_MAX;

    // Cached data for the currently selected death.
    std::vector<TargetCombatEvent> cachedEvents_;
    std::vector<ResourceEvent> cachedHealthHistory_;
    uint64_t cachedMaxHealth_ = 0;

    // Filter slider (0-20%): hide hits smaller than this % of max HP.
    // Default 7% so small ticks don't drown out the actual killing blows.
    float eventFilterThreshold_ = 7.0f;

    // Refresh cachedEvents_ / cachedHealthHistory_ for the selected death.
    void loadSelectedDeath(const CombatDatabase* combatDb,
                           const ResourceDatabase* resourceDb);

    // Render helpers - each takes the fallback map so spell names in
    // the tables resolve from the combat log rather than the (unloaded)
    // SpellDataTable in overlay mode.
    void renderDeathList();
    void renderSelectedDeathPanel(
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<uint32_t, std::string>* spellNameFallback);
    void renderHealthChart(float width, float height);
    void renderEventList(
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<uint32_t, std::string>* spellNameFallback);

    static std::string formatTimestamp(int32_t timestamp_ms);
    static std::string formatRelativeTime(int32_t offset_ms);
};
