#include "UI/Overlay/UIOverlayControls.h"
#include "UI/AwlUI/Widgets.h"
#include <cstdio>

bool UIOverlayControls::render(
    const LiveLogSession::Snapshot& snapshot,
    size_t selectedPullIndex,
    StatsViewMode currentMode,
    ViewModeCallback onViewModeChange,
    PullSelectCallback onPullSelect
) {
    bool changed = false;

    // View mode selector
    changed |= renderViewModeSelector(currentMode, onViewModeChange);

    ImGui::SameLine();

    // Pull history dropdown (only show when in CurrentPull or HistoricalPull mode)
    if (currentMode == StatsViewMode::CurrentPull || currentMode == StatsViewMode::HistoricalPull) {
        if (!snapshot.pullHistory.empty() || !snapshot.currentPull.label.empty()) {
            changed |= renderPullHistory(
                snapshot.pullHistory,
                snapshot.currentPull,
                selectedPullIndex,
                onPullSelect
            );
        }
    }

    return changed;
}

bool UIOverlayControls::renderViewModeSelector(StatsViewMode currentMode, ViewModeCallback onChange) {
    const char* currentLabel = getViewModeLabel(currentMode);
    bool changed = false;

    ImGui::SetNextItemWidth(100.0f);

    if (ImGui::BeginCombo("##viewmode", currentLabel, ImGuiComboFlags_NoArrowButton)) {
        StatsViewMode modes[] = {
            StatsViewMode::CurrentPull,
            StatsViewMode::CurrentDungeon,
            StatsViewMode::SessionTotal
        };

        for (auto mode : modes) {
            bool isSelected = (mode == currentMode);
            if (ImGui::Selectable(getViewModeLabel(mode), isSelected)) {
                if (onChange) onChange(mode);
                changed = true;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Select time range for stats");
    }

    return changed;
}

bool UIOverlayControls::renderPullHistory(
    const std::vector<PullSegment>& pulls,
    const PullSegment& currentPull,
    size_t selectedPullIndex,
    PullSelectCallback onSelect
) {
    // Current selection label
    // SIZE_MAX means "current/live" is selected (no historical selection)
    char currentLabel[64];
    if (selectedPullIndex != SIZE_MAX && selectedPullIndex < pulls.size()) {
        formatPullLabel(pulls[selectedPullIndex], currentLabel, sizeof(currentLabel), false);
    } else {
        formatPullLabel(currentPull, currentLabel, sizeof(currentLabel), true);
    }

    bool changed = false;

    ImGui::SetNextItemWidth(120.0f);

    if (ImGui::BeginCombo("##pullhistory", currentLabel, ImGuiComboFlags_NoArrowButton)) {
        // "Current" option at top
        {
            char label[64];
            snprintf(label, sizeof(label), "> %s (Live)", currentPull.label.c_str());

            bool isSelected = (selectedPullIndex == SIZE_MAX);
            if (ImGui::Selectable(label, isSelected)) {
                if (onSelect) onSelect(SIZE_MAX);  // SIZE_MAX = return to current
                changed = true;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::Separator();

        // Historical pulls in reverse order (most recent first)
        for (size_t i = pulls.size(); i > 0; --i) {
            size_t idx = i - 1;
            const auto& pull = pulls[idx];

            char label[64];
            formatPullLabel(pull, label, sizeof(label), false);

            bool isSelected = (selectedPullIndex == idx);
            if (ImGui::Selectable(label, isSelected)) {
                if (onSelect) onSelect(idx);
                changed = true;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Select a pull to view its stats");
    }

    return changed;
}

const char* UIOverlayControls::getViewModeLabel(StatsViewMode mode) {
    switch (mode) {
        case StatsViewMode::CurrentPull:    return "Current Pull";
        case StatsViewMode::HistoricalPull: return "Historical";
        case StatsViewMode::CurrentDungeon: return "Dungeon";
        case StatsViewMode::SessionTotal:   return "Session";
        default:                            return "Unknown";
    }
}

bool UIOverlayControls::renderLockToggle(bool isLocked, std::function<void(bool)> onToggle,
                                          int outScreenRect[4]) {
    // Locked = mouse passes through to WoW; we highlight this as the
    // "danger" state because it's how the user makes the overlay
    // unclickable. Unlocked = Ghost (subtle) since it's the default.
    const char* label = isLocked ? "Locked" : "Unlocked";
    const auto variant = isLocked
        ? awlui::ButtonVariant::Danger
        : awlui::ButtonVariant::Ghost;
    bool clicked = awlui::Button(label, variant, awlui::ButtonSize::Sm);

    if (outScreenRect) {
        // Overlay coords - caller adds window position for
        // screen-absolute WM_NCHITTEST math.
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        outScreenRect[0] = static_cast<int>(rmin.x);
        outScreenRect[1] = static_cast<int>(rmin.y);
        outScreenRect[2] = static_cast<int>(rmax.x);
        outScreenRect[3] = static_cast<int>(rmax.y);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(isLocked
            ? "Click to unlock. When locked, clicks pass through to\nthe game everywhere except this button."
            : "Overlay captures mouse input.\nClick to lock and let clicks pass through.\nCtrl+Left/Right cycles meter views.");
    }

    if (clicked && onToggle) {
        onToggle(!isLocked);
    }
    return clicked;
}

void UIOverlayControls::formatPullLabel(const PullSegment& pull, char* buffer, size_t bufferSize, bool isCurrent) {
    const char* durationStr = pull.getDurationString().c_str();

    if (isCurrent) {
        snprintf(buffer, bufferSize, "%s (%s)", pull.label.c_str(),
            pull.isInProgress() ? "..." : durationStr);
    } else {
        snprintf(buffer, bufferSize, "%s %s", pull.label.c_str(), durationStr);
    }
}
