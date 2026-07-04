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
#include "AuraDatabase.h"
#include "AvoidanceDatabase.h"
#include "AbsorbDatabase.h"
#include "Core/MeterBarRenderer.h"
#include "UI/SpellGroupSettings.h"

class ActorColorGenerator;
namespace awow { class ISpellIconRenderer; }
class DeathDatabase;
class ResourceDatabase;
struct DeathEvent;

// Meter view types - what metric to display
enum class MeterViewType {
    DamageDealt,           // DPS
    HealingDone,           // HPS
    DamageTaken,           // DTPS (damage taken by actor)
    DamageTakenByAbility,  // Damage taken grouped by source ability
    DamageTakenBy,         // Damage dealt TO selected target, grouped by source actor
    Dispels,               // Dispels only
    Interrupts,            // Interrupts only
    // New meter types
    Overhealing,           // Wasted healing (overheal amount)
    HealingTaken,          // Healing received by actor
    FriendlyFire,          // Damage dealt to friendly targets
    CCBreaks,              // Crowd control breaks
    Deaths,                // Death count by actor
    Avoidance,             // Dodge/parry/block/miss stats
    Absorbs,               // Damage prevented by shields
    EnemyDamage,           // Damage dealt by hostile actors (bosses/adds)
    EnemyHealing           // Healing cast by hostile actors
};

// Time range mode
enum class MeterTimeMode {
    Cumulative,       // From encounter start to current playback time
    FullEncounter     // Full encounter totals
};

// One phase window of the current pull. UIWindowManager builds these
// from the encounter's phase rules (first cast of a marked boss spell
// starts a new phase) and pushes the same list to every meter panel;
// the panel exposes them as a time filter next to the time mode combo.
struct MeterPhase {
    std::string label;      // "P1", "P2" or a user-provided name
    int32_t start_ms = 0;
    int32_t end_ms = 0;
};

// Panel configuration
struct MeterPanelConfig {
    MeterViewType view_type = MeterViewType::DamageDealt;
    MeterTimeMode time_mode = MeterTimeMode::Cumulative;
    bool show_percent = true;
    bool show_amount_per_second = true;
    float bar_height = 20.0f;
    float bar_spacing = 2.0f;
};

// Multi-instance meter panel (replaces separate damage/healing/taken meters)
// Each instance (1, 2, 3) can be configured to show different views
class UIMeterPanel {
public:
    // Constructor takes instance ID (1, 2, or 3) for unique window identification
    explicit UIMeterPanel(int instanceId);
    ~UIMeterPanel() = default;

    // Render the panel (creates its own ImGui window)
    // Returns true if window is still open
    bool render(
        const CombatDatabase* combatDb,
        const DispelDatabase* dispelDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader = nullptr,
        const std::unordered_map<std::string, std::string>* combatGuidToName = nullptr,
        const DeathDatabase* deathDb = nullptr,
        const ResourceDatabase* resourceDb = nullptr,
        const AuraDatabase* auraDb = nullptr,
        const AvoidanceDatabase* avoidanceDb = nullptr
    );

    // Render meter content directly without creating a window (for overlay mode)
    void renderEmbedded(
        const CombatDatabase* combatDb,
        const DispelDatabase* dispelDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader = nullptr,
        const std::unordered_map<std::string, std::string>* combatGuidToName = nullptr,
        const DeathDatabase* deathDb = nullptr,
        const ResourceDatabase* resourceDb = nullptr,
        const AuraDatabase* auraDb = nullptr,
        const AbsorbDatabase* absorbDb = nullptr
    );

    // Render only the meter content (no tab bar, no time selector - for compact overlay)
    // filterStartTime_ms: If non-zero, overrides database min timestamp for encounter filtering
    // filterEndTime_ms: If non-zero, overrides database max timestamp for segment filtering
    // spellNameFallback: overlay-mode name lookup keyed by spell ID.
    // LiveLogSession populates this from the combat log's own spell-name
    // field so drill-downs show real names without a SpellDataTable
    // binary loaded. Pass nullptr in the main-app path.
    void renderEmbeddedContent(
        const CombatDatabase* combatDb,
        const DispelDatabase* dispelDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader = nullptr,
        const std::unordered_map<std::string, std::string>* combatGuidToName = nullptr,
        const DeathDatabase* deathDb = nullptr,
        const ResourceDatabase* resourceDb = nullptr,
        const AuraDatabase* auraDb = nullptr,
        const AbsorbDatabase* absorbDb = nullptr,
        const AvoidanceDatabase* avoidanceDb = nullptr,
        int32_t filterStartTime_ms = 0,
        int32_t filterEndTime_ms = 0,
        const std::unordered_map<uint32_t, std::string>* spellNameFallback = nullptr
    );

    // Window visibility
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    void toggleVisible() { visible_ = !visible_; }
    bool* getVisiblePtr() { return &visible_; }

    // Instance identification
    int getInstanceId() const { return instanceId_; }

    // Get clicked actor GUID (for selection integration)
    std::optional<std::string> getClickedActor() const { return clickedActor_; }
    void clearClickedActor() { clickedActor_ = std::nullopt; }

    // Get breakdown request (for opening detailed breakdown panel)
    std::optional<ui::BreakdownRequest> getBreakdownRequest() const { return breakdownRequest_; }
    void clearBreakdownRequest() { breakdownRequest_ = std::nullopt; }

    // Access configuration
    MeterPanelConfig& getConfig() { return config_; }
    const MeterPanelConfig& getConfig() const { return config_; }

    // Target selection for "Taken By" view
    void setSelectedTarget(const std::string& target_guid) { selectedTargetGuid_ = target_guid; }
    const std::string& getSelectedTarget() const { return selectedTargetGuid_; }
    void clearSelectedTarget() { selectedTargetGuid_.clear(); }

    // Replace the phase list for the current pull. Resets the phase
    // selection and clears any phase time filter that was active.
    void setPhases(std::vector<MeterPhase> phases);

    // True once after the "+" button next to the phase combo was
    // clicked; the owner polls this to open the phase editor panel
    bool consumePhaseEditorRequest() {
        bool requested = phaseEditorRequested_;
        phaseEditorRequested_ = false;
        return requested;
    }

private:
    int instanceId_;
    std::string windowTitle_;   // "Meter 1", "Meter 2", etc.
    std::string tabBarId_;      // Unique tab bar ID per instance

    bool visible_ = false;
    MeterPanelConfig config_;
    std::optional<std::string> clickedActor_;
    std::optional<ui::BreakdownRequest> breakdownRequest_;
    std::string expandedActorGuid_;  // Which actor's spell breakdown is expanded

    // Lazy per-actor spell breakdown for the overlay path. The list
    // queries pass includeBreakdowns=false to keep the top-level ranking
    // cheap; when the user actually expands a row we fetch just that
    // actor's breakdown once and cache it here. Cleared whenever the
    // ranking cache is invalidated (view change, time filter change,
    // or scrub).
    std::string cachedExpandedActorGuid_;
    CombatMetricType cachedExpandedMetric_ = CombatMetricType::DamageDealt;
    std::vector<SpellCombatStats> cachedExpandedBreakdown_;
    std::string selectedTargetGuid_;  // Selected target for "Taken By" view
    spell_grouping::SpellGroupSettings spellGroupSettings_;  // For blacklist filtering

    // Throttling: only refresh stats once per second (reduces CPU load)
    static constexpr float REFRESH_INTERVAL_MS = 1000.0f;
    uint32_t lastRefreshTime_ms_ = 0;
    uint32_t lastPlaybackTime_ms_ = 0;

    // Encounter/segment filtering: if non-zero, overrides database timestamps
    int32_t filterStartTime_ms_ = 0;
    int32_t filterEndTime_ms_ = 0;
    int32_t cachedFilterStartTime_ms_ = 0;  // Track changes to filter for cache invalidation
    int32_t cachedFilterEndTime_ms_ = 0;

    // Boss phase filter: phases for the current pull, plus which one is
    // selected (-1 = all phases). Selecting a phase drives the same
    // filterStartTime_ms_/filterEndTime_ms_ window the segment filter uses.
    std::vector<MeterPhase> phases_;
    int selectedPhase_ = -1;

    // Raised by the "+" button next to the phase combo; the window
    // manager consumes it and opens the phase editor
    bool phaseEditorRequested_ = false;

    // Shows "Copied!" on the report button for a moment after copying
    float reportFeedbackTimer_ = 0.0f;

    // Cached data per view type
    std::vector<ActorCombatStats> cachedCombatStats_;
    std::vector<ActorDispelStats> cachedDispelStats_;
    std::vector<ActorCCBreakStats> cachedCCBreakStats_;
    std::vector<ActorAvoidanceStats> cachedAvoidanceStats_;
    std::vector<ActorAbsorbStats> cachedAbsorbStats_;
    std::vector<const DeathEvent*> cachedDeathEvents_;
    int64_t cachedGrandTotal_ = 0;
    uint32_t cachedDispelTotal_ = 0;
    uint32_t cachedCCBreakTotal_ = 0;
    uint32_t cachedAvoidanceTotal_ = 0;
    int64_t cachedAvoidanceAmount_ = 0;
    int64_t cachedAbsorbTotal_ = 0;
    uint32_t cachedDeathTotal_ = 0;
    uint32_t cachedDuration_ms_ = 0;
    // Window bounds the last ranking cache was computed over. Reused
    // by the lazy-expand path so the drill-down query lines up with
    // the numbers the user is looking at.
    int32_t cachedWindowStart_ms_ = 0;
    int32_t cachedWindowEnd_ms_ = 0;
    // Per-frame overlay fallback map, set at the top of
    // renderEmbeddedContent so the render*View helpers can pass it
    // down to renderCombatBarChart without threading a param through
    // every one of them.
    const std::unordered_map<uint32_t, std::string>* currentSpellNameFallback_ = nullptr;
    MeterViewType cachedViewType_ = MeterViewType::DamageDealt;
    MeterTimeMode cachedTimeMode_ = MeterTimeMode::Cumulative;

    // Render helpers
    void renderTabBar();
    // Time mode combo + phase filter + report-to-clipboard button.
    // Takes the name maps so the report line can resolve actor names.
    void renderTimeModeSelector(
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        const std::unordered_map<std::string, std::string>* combatGuidToName);
    void renderReportButton(
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        const std::unordered_map<std::string, std::string>* combatGuidToName);
    void renderDamageView(
        const CombatDatabase* combatDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderHealingView(
        const CombatDatabase* combatDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderDamageTakenView(
        const CombatDatabase* combatDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderDamageTakenByView(
        const CombatDatabase* combatDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderDispelView(
        const DispelDatabase* dispelDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderInterruptView(
        const DispelDatabase* dispelDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderOverhealingView(
        const CombatDatabase* combatDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderHealingTakenView(
        const CombatDatabase* combatDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderFriendlyFireView(
        const CombatDatabase* combatDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderCCBreaksView(
        const AuraDatabase* auraDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderDeathsView(
        const DeathDatabase* deathDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderAvoidanceView(
        const AvoidanceDatabase* avoidanceDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderAbsorbsView(
        const AbsorbDatabase* absorbDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderEnemyDamageView(
        const CombatDatabase* combatDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderEnemyHealingView(
        const CombatDatabase* combatDb,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t currentTime_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderCombatBarChart(
        const std::vector<ActorCombatStats>& stats,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        int64_t grandTotal,
        uint32_t duration_ms,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName,
        const ImVec4& barColor,
        // For lazy expand: passed by the overlay's render*View functions
        // so this method can query the per-actor spell breakdown only
        // when a row is actually expanded. Pass nullptr for combatDb to
        // fall back to actorStats.spell_breakdown (the main app path
        // already fetches full breakdowns eagerly).
        const CombatDatabase* combatDb = nullptr,
        CombatMetricType lazyMetric = CombatMetricType::DamageDealt,
        int32_t lazyWindowStart_ms = 0,
        int32_t lazyWindowEnd_ms = 0,
        const std::unordered_map<uint32_t, std::string>* spellNameFallback = nullptr
    );
    void renderDispelBarChart(
        const std::vector<ActorDispelStats>& stats,
        const std::shared_ptr<ActorColorGenerator>& colorGen,
        const std::unordered_map<std::string_view, std::string_view>& guidToName,
        uint32_t grandTotal,
        awow::ISpellIconRenderer* iconLoader,
        const std::unordered_map<std::string, std::string>* combatGuidToName
    );
    void renderTotalSummary(int64_t grandTotal, float grandAmountPerSecond);
    void renderDispelSummary(uint32_t dispelCount, uint32_t interruptCount);
};
