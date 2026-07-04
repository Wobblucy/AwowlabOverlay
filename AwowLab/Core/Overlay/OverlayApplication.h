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
    std::unique_ptr<UIOverlayControls> controls_;
    std::unique_ptr<UIStalenessIndicator> staleness_;

    // State
    std::filesystem::path logsFolder_;
    StatsViewMode viewMode_ = StatsViewMode::CurrentPull;

    // Segment selection (Details-style indexing)
    // SIZE_MAX = Current (live), SIZE_MAX-1 = Overall, 0+ = historical pull index
    static constexpr size_t SEGMENT_CURRENT = SIZE_MAX;
    static constexpr size_t SEGMENT_OVERALL = SIZE_MAX - 1;
    size_t selectedSegment_ = SEGMENT_CURRENT;

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
