#include "UI/Panels/Combat/MeterViewBase.h"
#include "UI/ISpellIconRenderer.h"
#include "Color/ActorColorGenerator.h"
#include "Core/NumberFormatter.h"
#include "Core/LocalizationManager.h"
#include <sstream>

namespace meter_rendering {

void renderCombatBarChart(
    const std::vector<ActorCombatStats>& stats,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    int64_t grandTotal,
    uint32_t duration_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    const ImVec4& barColor,
    int instanceId,
    std::optional<std::string>& clickedActor,
    std::optional<ui::BreakdownRequest>& breakdownRequest,
    std::string& expandedActorGuid
) {
    if (stats.empty()) return;

    float availableWidth = ImGui::GetContentRegionAvail().x;
    float maxAmount = static_cast<float>(stats[0].effective_amount);

    // Configure bar rendering
    ui::MeterBarConfig barConfig;
    barConfig.bar_height = 20.0f;
    barConfig.bar_spacing = 2.0f;
    barConfig.right_margin = 160.0f;
    barConfig.default_color = barColor;
    barConfig.show_percent = true;
    barConfig.show_per_second = true;
    barConfig.per_second_label = "DPS";
    barConfig.per_second_tooltip_label = "DPS";
    barConfig.allow_expansion = true;

    for (size_t i = 0; i < stats.size(); ++i) {
        const auto& actorStats = stats[i];
        bool isExpanded = (expandedActorGuid == actorStats.actor_guid);

        // Push unique ID for each bar based on instance
        ImGui::PushID(static_cast<int>(i * 10 + instanceId));

        ui::MeterBarResult result = ui::renderMeterBar(
            i, actorStats, colorGen, guidToName,
            grandTotal, duration_ms, maxAmount, availableWidth,
            barConfig, isExpanded, iconLoader, combatGuidToName
        );

        if (!result.clicked_actor.empty()) {
            clickedActor = result.clicked_actor;
            breakdownRequest = ui::BreakdownRequest{
                actorStats.actor_guid,
                actorStats,
                duration_ms
            };
        }
        if (!result.toggled_actor.empty()) {
            if (isExpanded) {
                expandedActorGuid.clear();
            } else {
                expandedActorGuid = result.toggled_actor;
            }
        }

        if (isExpanded && !actorStats.spell_breakdown.empty()) {
            ImGui::Indent(20.0f);
            ui::renderSpellBreakdown(actorStats.spell_breakdown, actorStats.effective_amount,
                                     ImVec4(0.5f, 0.5f, 0.6f, 0.8f), L("meter.melee"), iconLoader);
            ImGui::Unindent(20.0f);
        }

        ImGui::PopID();
    }
}

void renderDispelBarChart(
    const std::vector<ActorDispelStats>& stats,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t grandTotal,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    int instanceId
) {
    (void)grandTotal;  // Used for percentage calculation if needed
    (void)iconLoader;  // Reserved for future spell icon display
    if (stats.empty()) return;

    const float bar_height = 20.0f;
    const float bar_spacing = 2.0f;
    float availableWidth = ImGui::GetContentRegionAvail().x;
    uint32_t maxCount = stats[0].total_count;
    if (maxCount == 0) maxCount = 1;

    for (size_t i = 0; i < stats.size(); ++i) {
        const auto& actorStats = stats[i];

        // Resolve actor name (use explicit string_view to avoid iterator issues)
        std::string actorName;
        std::string_view guidView(actorStats.actor_guid);
        auto nameIt = guidToName.find(guidView);
        if (nameIt != guidToName.end()) {
            actorName = std::string(nameIt->second);
        } else if (combatGuidToName) {
            auto combatNameIt = combatGuidToName->find(actorStats.actor_guid);
            if (combatNameIt != combatGuidToName->end()) {
                actorName = combatNameIt->second;
            }
        }
        if (actorName.empty()) {
            actorName = actorStats.actor_guid.substr(0, 12);
        }

        // Truncate name if too long (UTF-8 safe)
        if (ui::utf8CharCount(actorName) > 12) {
            actorName = ui::utf8Truncate(actorName, 10) + "..";
        }

        // Get actor color
        ImVec4 barColor(0.6f, 0.3f, 0.6f, 1.0f);  // Purple for dispels
        if (colorGen) {
            ActorColor color = colorGen->getCachedColor(actorStats.actor_guid);
            barColor = ImVec4(color.r, color.g, color.b, 1.0f);
        }

        // Calculate bar width
        float barWidth = (availableWidth - 160.0f) * (static_cast<float>(actorStats.total_count) / maxCount);
        if (barWidth < 5.0f) barWidth = 5.0f;

        // Render bar
        ImGui::PushID(static_cast<int>(i * 10 + instanceId));

        ImVec2 cursorPos = ImGui::GetCursorPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetCursorScreenPos(),
            ImVec2(ImGui::GetCursorScreenPos().x + barWidth, ImGui::GetCursorScreenPos().y + bar_height),
            ImGui::ColorConvertFloat4ToU32(barColor)
        );

        // Actor name on bar
        ImGui::SetCursorPos(ImVec2(cursorPos.x + 5.0f, cursorPos.y + (bar_height - ImGui::GetTextLineHeight()) / 2.0f));
        ImGui::Text("%s", actorName.c_str());

        // Stats on the right
        ImGui::SetCursorPos(ImVec2(cursorPos.x + availableWidth - 150.0f, cursorPos.y + (bar_height - ImGui::GetTextLineHeight()) / 2.0f));
        ImGui::Text("%u (D:%u I:%u)", actorStats.total_count, actorStats.dispel_count, actorStats.interrupt_count);

        // Handle click - note: we don't have clickedActor as parameter, so we skip this for now
        ImGui::SetCursorPos(cursorPos);
        ImGui::InvisibleButton("##bar", ImVec2(availableWidth, bar_height));

        // Tooltip with breakdown
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", actorName.c_str());
            ImGui::Separator();
            ImGui::Text("Dispels: %u", actorStats.dispel_count);
            ImGui::Text("Interrupts: %u", actorStats.interrupt_count);
            ImGui::Text("Stolen: %u", actorStats.stolen_count);
            if (actorStats.failed_count > 0) {
                ImGui::Text("Failed: %u", actorStats.failed_count);
            }
            if (!actorStats.spell_breakdown.empty()) {
                ImGui::Separator();
                ImGui::Text("Breakdown:");
                for (const auto& spell : actorStats.spell_breakdown) {
                    const char* typeStr = "";
                    switch (spell.event_type) {
                        case DispelInterruptEvent::EventType::Dispel: typeStr = "D"; break;
                        case DispelInterruptEvent::EventType::Interrupt: typeStr = "I"; break;
                        case DispelInterruptEvent::EventType::Stolen: typeStr = "S"; break;
                        default: break;
                    }
                    ImGui::Text("  [%s] %s x%u", typeStr, spell.extra_spell_name.c_str(), spell.count);
                }
            }
            ImGui::EndTooltip();
        }

        ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + bar_height + bar_spacing));
        ImGui::PopID();
    }
}

void renderTotalSummary(int64_t grandTotal, float grandAmountPerSecond) {
    ImGui::Separator();

    std::string totalStr = ui::format::formatAmount(grandTotal);
    std::string apsStr = ui::format::formatPerSecond(grandAmountPerSecond);

    // Default to DPS format
    ImGui::Text(L("format.total_dps"), totalStr.c_str(), apsStr.c_str());
}

void renderDispelSummary(uint32_t dispelCount, uint32_t interruptCount) {
    ImGui::Separator();
    ImGui::Text("%s: %u  |  %s: %u", L("meter.dispel_count"), dispelCount, L("meter.interrupt_count"), interruptCount);
}

} // namespace meter_rendering
