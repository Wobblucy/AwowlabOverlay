#include "UI/Overlay/UIStalenessIndicator.h"
#include <cstdio>

void UIStalenessIndicator::render(std::chrono::seconds secondsSinceWrite) {
    int64_t seconds = secondsSinceWrite.count();
    ImVec4 color = getColor(seconds);

    char timeBuf[32];
    formatTime(seconds, timeBuf, sizeof(timeBuf));

    // Colored indicator circle
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();

    float radius = 4.0f;
    ImVec2 center = ImVec2(cursorPos.x + radius + 2.0f, cursorPos.y + ImGui::GetTextLineHeight() / 2.0f);

    ImU32 circleColor = ImGui::ColorConvertFloat4ToU32(color);
    drawList->AddCircleFilled(center, radius, circleColor);

    // Text
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + radius * 2 + 6.0f);
    ImGui::TextColored(color, "Last write: %s", timeBuf);

    // Tooltip on hover
    if (ImGui::IsItemHovered()) {
        renderTooltip(seconds);
    }
}

void UIStalenessIndicator::renderCompact(std::chrono::seconds secondsSinceWrite) {
    int64_t seconds = secondsSinceWrite.count();
    ImVec4 color = getColor(seconds);

    char timeBuf[32];
    formatTime(seconds, timeBuf, sizeof(timeBuf));

    // Just colored dot and short time
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();

    float radius = 3.0f;
    ImVec2 center = ImVec2(cursorPos.x + radius, cursorPos.y + ImGui::GetTextLineHeight() / 2.0f);

    ImU32 circleColor = ImGui::ColorConvertFloat4ToU32(color);
    drawList->AddCircleFilled(center, radius, circleColor);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + radius * 2 + 4.0f);
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", timeBuf);

    if (ImGui::IsItemHovered()) {
        renderTooltip(seconds);
    }
}

ImVec4 UIStalenessIndicator::getColor(int64_t seconds) {
    if (seconds < 10) {
        // Green - fresh
        return ImVec4(0.2f, 0.9f, 0.3f, 1.0f);
    } else if (seconds < 60) {
        // Yellow - somewhat stale
        return ImVec4(0.9f, 0.9f, 0.2f, 1.0f);
    } else if (seconds < 120) {
        // Orange - stale
        return ImVec4(0.9f, 0.6f, 0.2f, 1.0f);
    } else {
        // Red - very stale
        return ImVec4(0.9f, 0.3f, 0.2f, 1.0f);
    }
}

const char* UIStalenessIndicator::formatTime(int64_t seconds, char* buffer, size_t bufferSize) {
    if (seconds < 60) {
        snprintf(buffer, bufferSize, "%llds", seconds);
    } else if (seconds < 3600) {
        int minutes = static_cast<int>(seconds / 60);
        int secs = static_cast<int>(seconds % 60);
        snprintf(buffer, bufferSize, "%dm %ds", minutes, secs);
    } else {
        int hours = static_cast<int>(seconds / 3600);
        int minutes = static_cast<int>((seconds % 3600) / 60);
        snprintf(buffer, bufferSize, "%dh %dm", hours, minutes);
    }
    return buffer;
}

void UIStalenessIndicator::renderTooltip(int64_t seconds) {
    ImGui::BeginTooltip();

    if (seconds < 10) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Fresh");
    } else if (seconds < 60) {
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.2f, 1.0f), "Slightly behind");
    } else if (seconds < 120) {
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Stale");
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1.0f), "Very stale");
    }

    ImGui::Separator();
    ImGui::TextUnformatted(
        "WoW flushes the combat log to disk roughly every 2 minutes\n"
        "outside boss combat and only once - at the end - during boss\n"
        "combat. The countdown estimates when the next batch is due.");

    ImGui::EndTooltip();
}
