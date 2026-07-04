#pragma once

#include "imgui.h"
#include "Core/Overlay/LiveCombatStats.h"
#include "Core/Overlay/LiveLogSession.h"
#include "Core/Overlay/PullSegment.h"
#include <vector>
#include <functional>

// Control bar for the overlay meter.
// Provides:
// - View mode selector (Current Pull / Current Dungeon / Session)
// - Pull history dropdown (for navigating to historical pulls)
//
// Usage:
//   UIOverlayControls controls;
//   auto snapshot = session->getSnapshot();
//   controls.render(snapshot, stats.getSelectedHistoricalPullIndex(), viewMode, onViewModeChange, onPullSelect);
//
class UIOverlayControls {
public:
    using ViewModeCallback = std::function<void(StatsViewMode)>;
    using PullSelectCallback = std::function<void(size_t)>;  // Pull index

    UIOverlayControls() = default;

    // Render the control bar using snapshot data for thread safety
    // Returns true if any selection changed
    bool render(
        const LiveLogSession::Snapshot& snapshot,
        size_t selectedPullIndex,
        StatsViewMode currentMode,
        ViewModeCallback onViewModeChange,
        PullSelectCallback onPullSelect
    );

    // Render just the view mode selector
    bool renderViewModeSelector(StatsViewMode currentMode, ViewModeCallback onChange);

    // Render just the pull history dropdown
    // selectedPullIndex: SIZE_MAX means "current/live" is selected
    bool renderPullHistory(
        const std::vector<PullSegment>& pulls,
        const PullSegment& currentPull,
        size_t selectedPullIndex,
        PullSelectCallback onSelect
    );

    // Render a lock/unlock toggle button that flips click-through state.
    // When locked, mouse events pass through to whatever is behind the
    // overlay (i.e. the game window). When unlocked, the overlay
    // captures mouse input so the user can drag and click. Returns
    // true if the button was clicked this frame.
    //
    // If outScreenRect is non-null, it receives the button's screen
    // rectangle {left, top, right, bottom} so the caller can register
    // it as a click-through escape hatch.
    bool renderLockToggle(bool isLocked, std::function<void(bool)> onToggle,
                          int outScreenRect[4] = nullptr);

private:
    // Get label for view mode
    static const char* getViewModeLabel(StatsViewMode mode);

    // Format pull for dropdown
    static void formatPullLabel(const PullSegment& pull, char* buffer, size_t bufferSize, bool isCurrent);
};
