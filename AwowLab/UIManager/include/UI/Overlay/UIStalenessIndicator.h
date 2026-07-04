#pragma once

#include "imgui.h"
#include <chrono>

// Displays how long ago the combat log was last written to.
// Provides visual feedback about data freshness with color-coded indicator.
//
// Color thresholds:
// - Green:  < 10 seconds (fresh)
// - Yellow: 10-60 seconds (somewhat stale)
// - Orange: 1-2 minutes (stale)
// - Red:    > 2 minutes (very stale)
//
// Includes tooltip explaining WoW's intentional write buffering.
//
class UIStalenessIndicator {
public:
    UIStalenessIndicator() = default;

    // Render the indicator
    // secondsSinceWrite: how long since the log file was last written
    void render(std::chrono::seconds secondsSinceWrite);

    // Compact mode - just shows colored dot + time
    void renderCompact(std::chrono::seconds secondsSinceWrite);

private:
    // Get color based on staleness
    static ImVec4 getColor(int64_t seconds);

    // Format seconds as human-readable string
    static const char* formatTime(int64_t seconds, char* buffer, size_t bufferSize);

    // Tooltip explaining WoW's buffering
    static void renderTooltip(int64_t seconds);
};
