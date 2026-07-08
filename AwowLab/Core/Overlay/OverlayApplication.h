#pragma once

#include "LiveLogManager.h"
#include "LiveLogSession.h"
#include "LiveCombatStats.h"
#include "OverlayVulkanContext.h"
#include "UI/Overlay/OverlayWindow.h"
#include "UI/Panels/Combat/UIMeterPanel.h"
#include "UI/Panels/Combat/UIDeathRecapPanel.h"
#include "UI/Panels/Combat/UIActorBreakdownPanel.h"
#include "UI/Panels/Combat/UIAvoidanceBreakdownPanel.h"
#include "UI/Panels/Settings/UIMobWeightPanel.h"
#include "UI/Panels/Settings/UIPhaseEditorPanel.h"
#include "UI/Overlay/UIOverlayControls.h"
#include "UI/Overlay/UIStalenessIndicator.h"
#include "Color/ActorColorGenerator.h"
#include <filesystem>
#include <memory>
#include <string>
#include <mutex>

// Standalone overlay application for live combat log monitoring.
// This is a lightweight alternative to the full AwowLab application,
// designed to run as an always-on-top overlay during gameplay.
//
// Features:
// - Always-on-top, borderless window
// - Real-time DPS/HPS/DTPS meters
// - Pull history navigation
// - View mode switching (Current Pull / Dungeon / Session)
// - Staleness indicator
//
// Usage:
//   OverlayApplication app;
//   app.setLogsFolder("C:/.../_retail_/Logs");
//   return app.run();
//
class OverlayApplication {
public:
    OverlayApplication();
    ~OverlayApplication();

    // Set the WoW Logs folder to monitor
    void setLogsFolder(const std::filesystem::path& folder);

    // Standalone overlay: supply the font compiled into the executable.
    // Call before run(); the bytes must stay alive for the app's lifetime.
    // Without it (main app overlay mode) ImGui's built-in font is used.
    void setUiFont(const unsigned char* data, size_t size) {
        uiFontData_ = data;
        uiFontSize_ = size;
    }

    // Run the overlay application (main loop)
    // Returns exit code
    int run();

private:
    // Components
    std::unique_ptr<OverlayWindow> window_;
    std::unique_ptr<OverlayVulkanContext> vulkan_;
    std::unique_ptr<LiveLogManager> logManager_;
    std::unique_ptr<LiveCombatStats> stats_;
    std::shared_ptr<ActorColorGenerator> colorGen_;

    // UI components
    std::unique_ptr<UIMeterPanel> meterPanel_;
    std::unique_ptr<UIDeathRecapPanel> deathRecapPanel_;
    std::unique_ptr<UIActorBreakdownPanel> breakdownPanel_;
    std::unique_ptr<UIAvoidanceBreakdownPanel> avoidanceBreakdownPanel_;
    std::unique_ptr<UIMobWeightPanel> mobWeightPanel_;
    std::unique_ptr<UIPhaseEditorPanel> phaseEditorPanel_;
    std::unique_ptr<UIOverlayControls> controls_;
    std::unique_ptr<UIStalenessIndicator> staleness_;

    // State
    std::filesystem::path logsFolder_;
    StatsViewMode viewMode_ = StatsViewMode::CurrentPull;

    // Standalone-only extras (see the setters above)
    const unsigned char* uiFontData_ = nullptr;
    size_t uiFontSize_ = 0;

    // Settings popup (gear button on the top row). Holds the log-folder
    // display and the manual language picker. Toggled by the gear, drawn
    // as a floating window on top of the meter.
    bool settingsOpen_ = false;
    // Text buffer for the "add tracked defensive by spell id" input in the
    // settings popup.
    char trackedDefInput_[16] = {0};
    // Laid-out size of the settings window from its last render, fed to
    // updateModalAutoGrow so the OS window grows to fit it.
    float settingsMeasuredW_ = 0.0f;
    float settingsMeasuredH_ = 0.0f;
    void renderSettingsWindow();

    // Point the live log manager at a new folder chosen from the settings
    // popup: persists the choice, tears the manager down, and restarts it
    // on the new folder with the same callbacks and stats attachment as
    // the initial launch. No-op if the folder matches the current one.
    void changeLogFolder(const std::filesystem::path& newFolder);

    // Wire the log manager's data-update callback and attach the live
    // stats to its session. Shared by the initial start and by a folder
    // change so both paths hook up identically.
    void wireLogManagerCallbacks();

    // Push the user's tracked-defensive spell ids into the live session so
    // it captures buff applies/removes for them (for the death recap's
    // defensive summary). Called after every (re)attach and whenever the
    // tracked list is edited in Settings.
    void applyTrackedDefensives();

    // Segment selection (Details-style indexing)
    // SIZE_MAX = Current (live), SIZE_MAX-1 = Overall, 0+ = historical pull index
    static constexpr size_t SEGMENT_CURRENT = SIZE_MAX;
    static constexpr size_t SEGMENT_OVERALL = SIZE_MAX - 1;
    size_t selectedSegment_ = SEGMENT_CURRENT;

    // When a folder is first attached (launch or folder change), the log
    // on disk is usually a finished session, not one WoW is still writing.
    // In that case "Current" has no live data and the meter would sit
    // empty until the user clicks a segment. To always show the most
    // recent log immediately, the first data update after an attach
    // auto-selects the newest segment. Reset on every (re)attach; cleared
    // once we've done it or once the user picks a segment themselves.
    bool autoSelectNewestPending_ = false;
    // Select the newest historical segment (last run's Overall, else the
    // last pull). No-op if history is empty or the user already navigated.
    void selectNewestSegment();

    // Modal auto-grow state. The overlay window is deliberately small
    // for at-a-glance meter viewing, so any modal opened inside it
    // (breakdown, avoidance breakdown, death recap) is clamped to that
    // same tiny area. To keep drill-downs actually readable, we snap
    // the OS window to a bigger centered rect when any modal opens and
    // restore the previous geometry on close.
    bool modalGrowActive_ = false;
    int savedWindowX_ = 0;
    int savedWindowY_ = 0;
    int savedWindowW_ = 0;
    int savedWindowH_ = 0;
    void updateModalAutoGrow();

    // Cached snapshot - only updated when log data changes (via onDataUpdate callback)
    LiveLogSession::Snapshot cachedSnapshot_;
    mutable std::mutex snapshotMutex_;  // Protects cachedSnapshot_

    // Persistent player-spec store for class-colored meter bars. The
    // color generator keys on string_view, so the backing GUID strings
    // have to outlive it - this map owns them. Player specs don't
    // change, so it only grows (bounded by raid size) and is never
    // cleared mid-session. syncSpecColors() copies fresh spec ids out
    // of the snapshot into it and feeds string_views into colorGen_.
    std::unordered_map<std::string, uint16_t> specIdStore_;
    void syncSpecColors();

    // Rebuild the meter's phase list for the currently selected boss
    // segment, using the shared resolver over the live capture. No-op
    // (and hides the phase UI) for segments without an encounter id.
    void updatePhases();

    // Absolute desktop cursor position (window origin + in-client cursor).
    // Stable while the window moves, so window dragging reads it instead of
    // ImGui's window-relative mouse. False if there's no window yet.
    bool screenCursor(double& x, double& y) const;

    // Assemble the phase editor's plain-data input from the live
    // capture for the selected boss segment.
    PhaseEditorData buildPhaseEditorData();

    // Encounter id + pull window of the segment currently shown.
    // Cached each frame so the phase UI and the breakdown menu's
    // "starts a new phase here" agree on which encounter they edit.
    uint32_t currentPhaseEncounterId_ = 0;
    int32_t currentPullStart_ms_ = 0;
    int32_t currentPullEnd_ms_ = 0;

    // Initialization
    bool initWindow();
    bool initLogManager();

    // Hotkeys. On Windows we register a system-wide hotkey (default
    // Ctrl+Shift+L) that toggles the lock even when the game has
    // keyboard focus - otherwise there's no way out of click-through
    // mode. The meter-cycle keys (Ctrl+Left/Right) only need to work
    // when the overlay itself has focus, so they use GLFW polling.
    void registerGlobalHotkeys();
    void unregisterGlobalHotkeys();
    void pumpGlobalHotkeyMessages();
    void pollLocalHotkeys();

    void cycleMeterView(int direction);  // +1 = next view, -1 = previous

    // Main loop
    void processFrame();
    void renderUI();

    // Settings
    void loadSettings();
    void saveSettings();
};

// Helper to check if overlay mode was requested via command line
bool isOverlayModeRequested(int argc, char* argv[]);
bool isOverlayModeRequested(int argc, wchar_t* argv[]);

// Helper to get logs folder from command line
std::filesystem::path getLogsFolderFromArgs(int argc, char* argv[]);
std::filesystem::path getLogsFolderFromArgs(int argc, wchar_t* argv[]);
