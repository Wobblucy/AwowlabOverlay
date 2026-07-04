#pragma once
#include <cstdint>
#include <vector>
#include <imgui.h>
#include "Database/CombatDatabase.h"

// Renders the time-series damage/healing graph in the breakdown panel
// Supports drag-to-select time range and zoom functionality
class BreakdownGraphRenderer {
public:
    BreakdownGraphRenderer() = default;

    // Reset state for new actor
    void reset();

    // Update time series data from database
    void updateTimeSeries(const std::vector<DamageTimeBucket>& timeSeries,
                          const std::vector<uint32_t>& topSpellIds,
                          uint32_t dbMinTime, uint32_t dbMaxTime);

    // Render the graph
    // Returns true if time selection changed (caller should refresh stats)
    bool render(CombatMetricType metricType, uint32_t currentTime_ms);

    // Time selection accessors
    bool hasTimeSelection() const { return hasTimeSelection_; }
    uint32_t getSelectionStartTime() const { return selectionStartTime_; }
    uint32_t getSelectionEndTime() const { return selectionEndTime_; }

    // Reset time selection to full encounter
    void resetTimeSelection();

private:
    // Time series data
    std::vector<DamageTimeBucket> timeSeries_;
    std::vector<uint32_t> topSpellIds_;
    int64_t maxBucketDamage_ = 0;

    // Database time bounds
    uint32_t dbMinTime_ = 0;
    uint32_t dbMaxTime_ = 0;

    // Time range selection state
    bool hasTimeSelection_ = false;
    uint32_t selectionStartTime_ = 0;
    uint32_t selectionEndTime_ = 0;
    bool isDraggingSelection_ = false;
    float dragStartX_ = 0.0f;

    // Get color for a spell in the stacked bar chart
    ImU32 getSpellColor(uint32_t spellId) const;

    // Render the selection header with reset button
    // Returns true if reset was clicked
    bool renderSelectionHeader();

    // Render the actual graph bars and interaction
    // Returns true if selection changed
    bool renderGraphContent(uint32_t currentTime_ms);
};
