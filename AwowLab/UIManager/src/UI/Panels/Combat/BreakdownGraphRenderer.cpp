#include "UI/Panels/Combat/BreakdownGraphRenderer.h"
#include "UI/AwlUI/Widgets.h"
#include "Core/NumberFormatter.h"
#include "Core/LocalizationManager.h"
#include <algorithm>
#include <cstdio>

void BreakdownGraphRenderer::reset() {
    timeSeries_.clear();
    topSpellIds_.clear();
    maxBucketDamage_ = 0;
    dbMinTime_ = 0;
    dbMaxTime_ = 0;
    hasTimeSelection_ = false;
    selectionStartTime_ = 0;
    selectionEndTime_ = 0;
    isDraggingSelection_ = false;
    dragStartX_ = 0.0f;
}

void BreakdownGraphRenderer::updateTimeSeries(const std::vector<DamageTimeBucket>& timeSeries,
                                               const std::vector<uint32_t>& topSpellIds,
                                               uint32_t dbMinTime, uint32_t dbMaxTime) {
    timeSeries_ = timeSeries;
    topSpellIds_ = topSpellIds;
    dbMinTime_ = dbMinTime;
    dbMaxTime_ = dbMaxTime;

    // Find max bucket damage for Y-axis scaling
    maxBucketDamage_ = 0;
    for (const auto& bucket : timeSeries_) {
        maxBucketDamage_ = std::max(maxBucketDamage_, bucket.total_damage);
    }
}

void BreakdownGraphRenderer::resetTimeSelection() {
    hasTimeSelection_ = false;
    selectionStartTime_ = 0;
    selectionEndTime_ = 0;
}

bool BreakdownGraphRenderer::render(CombatMetricType metricType, uint32_t currentTime_ms) {
    bool isHealing = (metricType == CombatMetricType::HealingDone);
    bool isDamageTaken = (metricType == CombatMetricType::DamageTaken);

    if (timeSeries_.empty() || maxBucketDamage_ == 0) {
        const char* metricName = isHealing ? L("meter.healing_done")
                               : (isDamageTaken ? L("meter.damage_taken") : L("meter.damage_done"));
        ImGui::TextDisabled(L("breakdown.no_data_graph"), metricName);
        return false;
    }

    bool selectionChanged = renderSelectionHeader();
    selectionChanged |= renderGraphContent(currentTime_ms);

    return selectionChanged;
}

bool BreakdownGraphRenderer::renderSelectionHeader() {
    bool selectionChanged = false;

    if (hasTimeSelection_) {
        uint32_t selDuration = selectionEndTime_ - selectionStartTime_;
        uint32_t selMinutes = selDuration / 60000;
        uint32_t selSeconds = (selDuration % 60000) / 1000;

        uint32_t startOffset = selectionStartTime_ - dbMinTime_;
        uint32_t startMin = startOffset / 60000;
        uint32_t startSec = (startOffset % 60000) / 1000;

        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
            L("breakdown.selection_info"),
            startMin, startSec,
            startMin + selMinutes, (startSec + selSeconds) % 60,
            selMinutes, selSeconds);
        ImGui::SameLine();
        if (awlui::Button(L("breakdown.reset_selection"),
                          awlui::ButtonVariant::Ghost,
                          awlui::ButtonSize::Sm)) {
            hasTimeSelection_ = false;
            selectionChanged = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", L("breakdown.drag_to_select_hint"));
    } else {
        ImGui::TextDisabled("%s", L("breakdown.select_time_hint"));
    }

    return selectionChanged;
}

bool BreakdownGraphRenderer::renderGraphContent(uint32_t currentTime_ms) {
    bool selectionChanged = false;

    // Determine time range to display (zoom to selection if active)
    uint32_t displayStartTime = dbMinTime_;
    uint32_t displayEndTime = dbMaxTime_;
    if (hasTimeSelection_) {
        displayStartTime = selectionStartTime_;
        displayEndTime = selectionEndTime_;
    }
    uint32_t displayDuration = (displayEndTime > displayStartTime) ? (displayEndTime - displayStartTime) : 1;

    // Find the bucket indices for the display range
    size_t startBucket = 0;
    size_t endBucket = timeSeries_.size();
    if (hasTimeSelection_ && !timeSeries_.empty()) {
        for (size_t i = 0; i < timeSeries_.size(); ++i) {
            if (timeSeries_[i].timestamp_ms < static_cast<int32_t>(displayStartTime)) {
                startBucket = i + 1;
            }
            if (timeSeries_[i].timestamp_ms >= static_cast<int32_t>(displayEndTime) && endBucket == timeSeries_.size()) {
                endBucket = i + 1;
                break;
            }
        }
    }
    size_t visibleBuckets = (endBucket > startBucket) ? (endBucket - startBucket) : 1;

    // Calculate max damage in visible range for proper Y-axis scaling when zoomed
    int64_t visibleMaxDamage = 0;
    for (size_t i = startBucket; i < endBucket && i < timeSeries_.size(); ++i) {
        visibleMaxDamage = std::max(visibleMaxDamage, timeSeries_[i].total_damage);
    }
    if (visibleMaxDamage == 0) visibleMaxDamage = maxBucketDamage_;

    const float graphHeight = 120.0f;
    ImVec2 graphSize(ImGui::GetContentRegionAvail().x, graphHeight);
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Background
    drawList->AddRectFilled(
        cursorPos,
        ImVec2(cursorPos.x + graphSize.x, cursorPos.y + graphSize.y),
        IM_COL32(20, 20, 30, 255)
    );

    // Border
    drawList->AddRect(
        cursorPos,
        ImVec2(cursorPos.x + graphSize.x, cursorPos.y + graphSize.y),
        IM_COL32(60, 60, 80, 255)
    );

    // Calculate bar width based on visible buckets
    float barWidth = graphSize.x / static_cast<float>(visibleBuckets);
    float barGap = (barWidth > 3.0f) ? 1.0f : 0.0f;
    if (barWidth < 1.0f) barWidth = 1.0f;

    // Draw stacked bars for each visible time bucket
    for (size_t i = startBucket; i < endBucket && i < timeSeries_.size(); ++i) {
        const auto& bucket = timeSeries_[i];
        if (bucket.total_damage == 0) continue;

        float x = cursorPos.x + (i - startBucket) * barWidth;
        float baseY = cursorPos.y + graphSize.y;
        float currentY = baseY;

        for (const auto& [spellId, damage] : bucket.by_spell) {
            float heightRatio = static_cast<float>(damage) / static_cast<float>(visibleMaxDamage);
            float barHeight = heightRatio * graphSize.y;
            if (barHeight < 0.5f) continue;

            ImU32 color = getSpellColor(spellId);
            drawList->AddRectFilled(
                ImVec2(x, currentY - barHeight),
                ImVec2(x + barWidth - barGap, currentY),
                color
            );
            currentY -= barHeight;
        }
    }

    // Draw selection highlight only when dragging (not when zoomed in)
    if (isDraggingSelection_) {
        float mouseX = ImGui::GetMousePos().x;
        float selStartX = std::min(dragStartX_, mouseX);
        float selEndX = std::max(dragStartX_, mouseX);

        // Clamp to graph bounds
        selStartX = std::max(selStartX, cursorPos.x);
        selEndX = std::min(selEndX, cursorPos.x + graphSize.x);

        // Draw selection overlay
        drawList->AddRectFilled(
            ImVec2(selStartX, cursorPos.y),
            ImVec2(selEndX, cursorPos.y + graphSize.y),
            IM_COL32(80, 150, 255, 60)
        );

        // Selection edges
        drawList->AddLine(
            ImVec2(selStartX, cursorPos.y),
            ImVec2(selStartX, cursorPos.y + graphSize.y),
            IM_COL32(80, 150, 255, 200), 2.0f
        );
        drawList->AddLine(
            ImVec2(selEndX, cursorPos.y),
            ImVec2(selEndX, cursorPos.y + graphSize.y),
            IM_COL32(80, 150, 255, 200), 2.0f
        );
    }

    // Draw red playback position indicator line
    if (currentTime_ms >= displayStartTime && currentTime_ms <= displayEndTime) {
        float playbackProgress = static_cast<float>(currentTime_ms - displayStartTime) /
                                 static_cast<float>(displayDuration);
        float playbackX = cursorPos.x + playbackProgress * graphSize.x;
        drawList->AddLine(
            ImVec2(playbackX, cursorPos.y),
            ImVec2(playbackX, cursorPos.y + graphSize.y),
            IM_COL32(255, 60, 60, 255), 2.0f
        );
    }

    // Draw Y-axis label (use visible max when zoomed)
    std::string maxLabel = ui::format::formatAmount(visibleMaxDamage);
    drawList->AddText(
        ImVec2(cursorPos.x + 4, cursorPos.y + 2),
        IM_COL32(200, 200, 200, 255),
        maxLabel.c_str()
    );

    // Draw time axis labels (show display range, not full encounter)
    uint32_t startOffset = displayStartTime - dbMinTime_;
    uint32_t startMin = startOffset / 60000;
    uint32_t startSec = (startOffset % 60000) / 1000;
    uint32_t endOffset = displayEndTime - dbMinTime_;
    uint32_t endMin = endOffset / 60000;
    uint32_t endSec = (endOffset % 60000) / 1000;

    char startLabel[16];
    snprintf(startLabel, sizeof(startLabel), "%u:%02u", startMin, startSec);
    drawList->AddText(
        ImVec2(cursorPos.x + 2, cursorPos.y + graphSize.y - 14),
        IM_COL32(150, 150, 150, 255),
        startLabel
    );

    char endLabel[16];
    snprintf(endLabel, sizeof(endLabel), "%u:%02u", endMin, endSec);
    ImVec2 endTextSize = ImGui::CalcTextSize(endLabel);
    drawList->AddText(
        ImVec2(cursorPos.x + graphSize.x - endTextSize.x - 2, cursorPos.y + graphSize.y - 14),
        IM_COL32(150, 150, 150, 255),
        endLabel
    );

    // Reserve space and handle mouse interaction
    ImGui::InvisibleButton("DamageGraphInteraction", graphSize);

    // Handle hover tooltip
    if (ImGui::IsItemHovered()) {
        float mouseX = ImGui::GetMousePos().x - cursorPos.x;
        float progress = mouseX / graphSize.x;
        progress = std::max(0.0f, std::min(1.0f, progress));

        uint32_t hoverTime = displayStartTime + static_cast<uint32_t>(progress * displayDuration);
        uint32_t hoverOffset = hoverTime - dbMinTime_;
        uint32_t hoverMin = hoverOffset / 60000;
        uint32_t hoverSec = (hoverOffset % 60000) / 1000;

        ImGui::SetTooltip(L("breakdown.hover_tooltip"), hoverMin, hoverSec);
    }

    // Handle drag to select
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        if (!isDraggingSelection_) {
            // Start new drag
            isDraggingSelection_ = true;
            dragStartX_ = ImGui::GetMousePos().x;
        }
    }

    // Handle drag release
    if (isDraggingSelection_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        isDraggingSelection_ = false;

        float mouseX = ImGui::GetMousePos().x;
        float startX = std::min(dragStartX_, mouseX) - cursorPos.x;
        float endX = std::max(dragStartX_, mouseX) - cursorPos.x;

        // Convert X positions to timestamps (relative to display range, not full encounter)
        float startProgress = std::max(0.0f, std::min(1.0f, startX / graphSize.x));
        float endProgress = std::max(0.0f, std::min(1.0f, endX / graphSize.x));

        selectionStartTime_ = displayStartTime + static_cast<uint32_t>(startProgress * displayDuration);
        selectionEndTime_ = displayStartTime + static_cast<uint32_t>(endProgress * displayDuration);

        // Only set selection if it's at least 1 second
        if (selectionEndTime_ - selectionStartTime_ >= 1000) {
            hasTimeSelection_ = true;
            selectionChanged = true;
        }
    }

    ImGui::Spacing();

    return selectionChanged;
}

ImU32 BreakdownGraphRenderer::getSpellColor(uint32_t spellId) const {
    // Predefined color palette for spells (familiar log-site colors)
    static const ImU32 colors[] = {
        IM_COL32(255, 128, 128, 200),  // Light red
        IM_COL32(128, 255, 128, 200),  // Light green
        IM_COL32(128, 128, 255, 200),  // Light blue
        IM_COL32(255, 255, 128, 200),  // Yellow
        IM_COL32(255, 128, 255, 200),  // Pink
        IM_COL32(128, 255, 255, 200),  // Cyan
        IM_COL32(255, 200, 128, 200),  // Orange
        IM_COL32(200, 128, 255, 200),  // Purple
        IM_COL32(128, 200, 128, 200),  // Forest green
        IM_COL32(200, 200, 128, 200),  // Olive
        IM_COL32(128, 200, 200, 200),  // Teal
        IM_COL32(200, 128, 200, 200),  // Magenta
        IM_COL32(180, 180, 180, 200),  // Gray
        IM_COL32(255, 160, 160, 200),  // Salmon
        IM_COL32(160, 255, 160, 200),  // Mint
    };
    static const size_t numColors = sizeof(colors) / sizeof(colors[0]);

    // Find index of this spell in our top spells list
    for (size_t i = 0; i < topSpellIds_.size(); ++i) {
        if (topSpellIds_[i] == spellId) {
            return colors[i % numColors];
        }
    }

    // Default color for spells not in top list
    return IM_COL32(100, 100, 100, 180);
}
