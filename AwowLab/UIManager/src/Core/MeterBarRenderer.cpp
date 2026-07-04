#include "Core/MeterBarRenderer.h"
#include "Core/SpellNameDatabase.h"
#include "Core/LocalizationManager.h"
#include "UI/ISpellIconRenderer.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

namespace ui {

// Look up a spell name preferring the caller-supplied fallback (typically
// LiveLogSession's spellIdToName, which is populated straight from the
// combat log's own name field). Falls back to SPELL_NAME (SpellDataTable)
// so the main app path is unchanged.
static const char* resolveSpellName(
    uint32_t spellId,
    const std::unordered_map<uint32_t, std::string>* fallback
) {
    if (fallback) {
        auto it = fallback->find(spellId);
        if (it != fallback->end() && !it->second.empty()) {
            return it->second.c_str();
        }
    }
    return SPELL_NAME(spellId);
}

// Implementation of the forward-declared function to avoid circular dependency
ImTextureID getSpellIconSafe(awow::ISpellIconRenderer* iconLoader, uint32_t spellId) {
    if (!iconLoader) {
        return 0;
    }
    return iconLoader->getSpellIcon(spellId);
}

MeterBarResult renderMeterBar(
    size_t index,
    const ActorCombatStats& stats,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    int64_t grandTotal,
    uint32_t duration_ms,
    float maxAmount,
    float availableWidth,
    const MeterBarConfig& config,
    [[maybe_unused]] bool isExpanded,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    const std::unordered_map<uint32_t, std::string>* spellNameFallback
) {
    MeterBarResult result;

    // Check if this is the virtual Blacklist actor
    bool isBlacklistActor = (stats.actor_guid == BLACKLIST_ACTOR_GUID);

    // Get actor name (special handling for Blacklist actor)
    std::string displayName;
    if (isBlacklistActor) {
        displayName = L("meter.blacklist");
    } else {
        displayName = ui::resolveActorName(stats.actor_guid, guidToName, 12, combatGuidToName);
    }

    // Get actor color (Blacklist actor gets dark gray)
    ImVec4 barColor = config.default_color;
    if (isBlacklistActor) {
        barColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);  // Dark gray for blacklist
    } else if (config.blend_with_actor_color && colorGen) {
        auto color = colorGen->getCachedColor(stats.actor_guid);
        barColor = ImVec4(
            color.r * config.color_blend.x,
            color.g * config.color_blend.y,
            color.b * config.color_blend.z,
            1.0f
        );
    }

    // Calculate bar width as percentage of max (use effective_amount to exclude overkill)
    float barPercent = (maxAmount > 0) ? static_cast<float>(stats.effective_amount) / maxAmount : 0.0f;
    float barWidth = barPercent * (availableWidth - config.right_margin);
    if (barWidth < 1.0f) barWidth = 1.0f;

    // Build label strings (use effective_amount to exclude overkill)
    std::string amountStr = ui::format::formatAmount(stats.effective_amount);
    std::string percentStr;
    if (config.show_percent && grandTotal > 0) {
        float percent = static_cast<float>(stats.effective_amount) / static_cast<float>(grandTotal) * 100.0f;
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << percent << "%";
        percentStr = oss.str();
    }
    std::string perSecStr;
    if (config.show_per_second && duration_ms > 0) {
        float ps = static_cast<float>(stats.effective_amount) / (static_cast<float>(duration_ms) / 1000.0f);
        perSecStr = ui::format::formatPerSecond(ps);
    }

    // Create unique ID
    ImGui::PushID(static_cast<int>(index));

    // Draw bar background
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Darker background
    ImVec4 bgColor(barColor.x * 0.3f, barColor.y * 0.3f, barColor.z * 0.3f, 0.8f);
    drawList->AddRectFilled(
        cursorPos,
        ImVec2(cursorPos.x + availableWidth - config.right_margin, cursorPos.y + config.bar_height),
        ImGui::ColorConvertFloat4ToU32(bgColor),
        3.0f
    );

    // Draw filled bar
    drawList->AddRectFilled(
        cursorPos,
        ImVec2(cursorPos.x + barWidth, cursorPos.y + config.bar_height),
        ImGui::ColorConvertFloat4ToU32(barColor),
        3.0f
    );

    // Draw name inside bar with shadow
    ImVec2 textPos(cursorPos.x + 4.0f, cursorPos.y + (config.bar_height - ImGui::GetTextLineHeight()) * 0.5f);
    drawList->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f),
                     ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 0.7f)),
                     displayName.c_str());
    drawList->AddText(textPos,
                     ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 1)),
                     displayName.c_str());

    // Invisible button for interaction
    ImGui::InvisibleButton("##bar", ImVec2(availableWidth - config.right_margin, config.bar_height));

    if (config.allow_selection && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        result.clicked_actor = stats.actor_guid;
    }
    if (config.allow_expansion && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        result.toggled_actor = stats.actor_guid;
    }

    // Tooltip on hover with Spells, Targets, and Pets breakdown
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();

        // Header
        ImGui::Text("%s", displayName.c_str());

        // Show overhealing info for healing meters
        if (config.show_overhealing && stats.total_amount > stats.effective_amount) {
            std::string effectiveStr = ui::format::formatAmount(stats.effective_amount);
            int64_t overhealAmount = stats.total_amount - stats.effective_amount;
            float overhealPct = (stats.total_amount > 0)
                ? static_cast<float>(overhealAmount) / static_cast<float>(stats.total_amount) * 100.0f : 0.0f;
            ImGui::Text(L("format.effective_total_overheal"),
                       effectiveStr.c_str(), amountStr.c_str(), overhealPct);
        } else {
            ImGui::Text(L("format.total_persec"), amountStr.c_str(),
                       config.per_second_tooltip_label, perSecStr.c_str());
        }
        ImGui::Text(L("format.hits_crits"), stats.hit_count, stats.crit_count);

        // ====== SPELLS SECTION ======
        if (!stats.spell_breakdown.empty()) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", L("breakdown.spells"));

            const float iconSize = 16.0f;
            size_t spellCount = std::min(stats.spell_breakdown.size(), size_t(8));

            for (size_t i = 0; i < spellCount; ++i) {
                const auto& spell = stats.spell_breakdown[i];

                // Draw spell icon if available
                if (iconLoader) {
                    if (iconLoader->renderSpellIcon(spell.spell_id, ImVec2(iconSize, iconSize))) {
                        ImGui::SameLine();
                    }
                }

                // Spell ID and stats
                float spellPercent = (stats.total_amount > 0) ?
                    static_cast<float>(spell.total_amount) / static_cast<float>(stats.total_amount) * 100.0f : 0.0f;

                std::string spellLabel = (spell.spell_id == 1)
                    ? std::string(L("meter.melee"))
                    : std::string(resolveSpellName(spell.spell_id, spellNameFallback));
                ImGui::Text("%s: %s (%.1f%%)", spellLabel.c_str(),
                           ui::format::formatAmount(spell.total_amount).c_str(), spellPercent);
            }
        }

        // ====== TARGETS SECTION ======
        if (!stats.target_breakdown.empty()) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", L("breakdown.targets"));

            size_t targetCount = std::min(stats.target_breakdown.size(), size_t(5));

            for (size_t i = 0; i < targetCount; ++i) {
                const auto& target = stats.target_breakdown[i];

                std::string targetName = ui::resolveActorName(target.target_guid, guidToName, 20, combatGuidToName);
                float targetPercent = (stats.total_amount > 0) ?
                    static_cast<float>(target.total_amount) / static_cast<float>(stats.total_amount) * 100.0f : 0.0f;

                ImGui::Text("%s: %s (%.1f%%)", targetName.c_str(),
                           ui::format::formatAmount(target.total_amount).c_str(), targetPercent);
            }
        }

        // ====== PETS SECTION ======
        if (!stats.pet_breakdown.empty()) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.8f, 1.0f), "%s", L("filter.pets"));

            for (const auto& pet : stats.pet_breakdown) {
                std::string petName = ui::resolveActorName(pet.pet_guid, guidToName, 20, combatGuidToName);
                float petPercent = (stats.total_amount > 0) ?
                    static_cast<float>(pet.total_amount) / static_cast<float>(stats.total_amount) * 100.0f : 0.0f;

                ImGui::Text("%s: %s (%.1f%%)", petName.c_str(),
                           ui::format::formatAmount(pet.total_amount).c_str(), petPercent);
            }
        }

        ImGui::Separator();
        if (config.allow_expansion) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", L("meter.click_hint"));
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Left-click to select");
        }
        ImGui::EndTooltip();
    }

    // Draw stats to the right
    ImGui::SameLine();

    std::string rightText;
    if (!percentStr.empty()) {
        rightText = percentStr;
    }
    if (!perSecStr.empty()) {
        if (!rightText.empty()) rightText += "  ";
        rightText += perSecStr;
    }
    // Show overheal % for healing meters
    if (config.show_overhealing && stats.total_amount > 0) {
        float overhealPct = (stats.total_amount > stats.effective_amount)
            ? static_cast<float>(stats.total_amount - stats.effective_amount) / static_cast<float>(stats.total_amount) * 100.0f
            : 0.0f;
        std::ostringstream ohOss;
        ohOss << std::fixed << std::setprecision(0) << overhealPct << "% OH";
        if (!rightText.empty()) rightText += "  ";
        rightText += ohOss.str();
    }
    if (!amountStr.empty()) {
        if (!rightText.empty()) rightText += "  ";
        rightText += amountStr;
    }

    ImGui::TextDisabled("%s", rightText.c_str());

    ImGui::PopID();

    // Add spacing between bars
    ImGui::Dummy(ImVec2(0, config.bar_spacing));

    return result;
}

void renderSpellBreakdown(
    const std::vector<SpellCombatStats>& spells,
    int64_t actorTotal,
    const ImVec4& barColor,
    const char* zeroSpellLabel,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<uint32_t, std::string>* spellNameFallback
) {
    if (spells.empty()) return;

    float availableWidth = ImGui::GetContentRegionAvail().x - 40.0f;
    float maxAmount = static_cast<float>(spells[0].total_amount);
    const float iconSize = 16.0f;
    const float barHeight = 18.0f;

    ImVec4 bgColor(barColor.x * 0.4f, barColor.y * 0.4f, barColor.z * 0.4f, 0.6f);

    for (size_t i = 0; i < std::min(spells.size(), size_t(10)); ++i) {
        const auto& spell = spells[i];

        // Spell name (with fallback)
        std::string spellLabel = (spell.spell_id == 1)
            ? std::string(zeroSpellLabel)
            : std::string(resolveSpellName(spell.spell_id, spellNameFallback));

        // Calculate bar width
        float barPercent = (maxAmount > 0) ? static_cast<float>(spell.total_amount) / maxAmount : 0.0f;
        float barWidth = barPercent * (availableWidth - 140.0f);
        if (barWidth < 1.0f) barWidth = 1.0f;

        // Calculate percentage of actor's total
        float percentOfActor = (actorTotal > 0) ?
            static_cast<float>(spell.total_amount) / static_cast<float>(actorTotal) * 100.0f : 0.0f;

        ImGui::PushID(static_cast<int>(i + 1000));

        // Draw spell icon if available
        if (iconLoader) {
            if (iconLoader->renderSpellIcon(spell.spell_id, ImVec2(iconSize, iconSize))) {
                ImGui::SameLine();
            }
        }

        // Draw mini bar
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        float effectiveBarWidth = availableWidth - 140.0f;
        if (iconLoader) {
            effectiveBarWidth -= (iconSize + 4.0f);
        }

        drawList->AddRectFilled(
            cursorPos,
            ImVec2(cursorPos.x + effectiveBarWidth, cursorPos.y + barHeight),
            ImGui::ColorConvertFloat4ToU32(bgColor),
            2.0f
        );

        float scaledBarWidth = barPercent * effectiveBarWidth;
        if (scaledBarWidth < 1.0f) scaledBarWidth = 1.0f;

        drawList->AddRectFilled(
            cursorPos,
            ImVec2(cursorPos.x + scaledBarWidth, cursorPos.y + barHeight),
            ImGui::ColorConvertFloat4ToU32(barColor),
            2.0f
        );

        // Spell ID text
        ImVec2 textPos(cursorPos.x + 4.0f, cursorPos.y + (barHeight - ImGui::GetTextLineHeight()) * 0.5f);
        drawList->AddText(textPos,
                         ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.9f, 0.9f, 1.0f)),
                         spellLabel.c_str());

        ImGui::InvisibleButton("##spellbar", ImVec2(effectiveBarWidth, barHeight));

        // Tooltip with spell details
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", spellLabel.c_str());
            ImGui::Text("Total: %s", ui::format::formatAmount(spell.total_amount).c_str());
            if (spell.effective_amount != spell.total_amount) {
                ImGui::Text("Effective: %s", ui::format::formatAmount(spell.effective_amount).c_str());
            }
            ImGui::Text("Hits: %u", spell.hit_count);
            if (spell.crit_count > 0) {
                float critPercent = static_cast<float>(spell.crit_count) / static_cast<float>(spell.hit_count) * 100.0f;
                ImGui::Text("Crits: %u (%.1f%%)", spell.crit_count, critPercent);
            }
            if (spell.max_hit > 0) {
                ImGui::Text("Max Hit: %s", ui::format::formatAmount(spell.max_hit).c_str());
            }
            ImGui::EndTooltip();
        }

        ImGui::SameLine();

        // Stats on right
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << percentOfActor << "%  " << ui::format::formatAmount(spell.total_amount);
        ImGui::TextDisabled("%s", oss.str().c_str());

        ImGui::PopID();
    }
}

} // namespace ui
