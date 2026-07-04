#include "UI/Panels/Combat/UIAvoidanceBreakdownPanel.h"
#include "UI/ISpellIconRenderer.h"
#include "UI/AwlUI/Widgets.h"
#include "Core/NumberFormatter.h"
#include "Core/LocalizationManager.h"
#include "imgui.h"
#include <algorithm>

namespace {

// Local helper - same MissType label the meter tooltip uses. Kept local
// to avoid inventing a shared enum-to-string table just for this panel.
const char* missTypeLabel(MissType type) {
    switch (type) {
        case MissType::DODGE:   return "Dodge";
        case MissType::PARRY:   return "Parry";
        case MissType::BLOCK:   return "Block";
        case MissType::MISS:    return "Miss";
        case MissType::DEFLECT: return "Deflect";
        case MissType::IMMUNE:  return "Immune";
        case MissType::RESIST:  return "Resist";
        case MissType::REFLECT: return "Reflect";
        case MissType::ABSORB:  return "Absorb";
        default:                return "Other";
    }
}

// Same fallback pattern the meter panels use for spell names.
const char* resolveSpellName(uint32_t spellId,
                             const std::string& providedName,
                             const std::unordered_map<uint32_t, std::string>* fallback) {
    if (!providedName.empty()) return providedName.c_str();
    if (fallback) {
        auto it = fallback->find(spellId);
        if (it != fallback->end() && !it->second.empty()) {
            return it->second.c_str();
        }
    }
    return providedName.c_str();  // empty
}

}  // namespace

void UIAvoidanceBreakdownPanel::open(const std::string& targetGuid,
                                     const std::string& targetName,
                                     const AvoidanceBreakdown& breakdown,
                                     uint32_t windowDuration_ms) {
    targetGuid_ = targetGuid;
    targetName_ = targetName;
    breakdown_ = breakdown;
    windowDuration_ms_ = windowDuration_ms;
    isOpen_ = true;
    shouldOpenPopup_ = true;
}

void UIAvoidanceBreakdownPanel::close() {
    isOpen_ = false;
    targetGuid_.clear();
    targetName_.clear();
    breakdown_ = {};
    windowDuration_ms_ = 0;
}

void UIAvoidanceBreakdownPanel::render(
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    const std::unordered_map<uint32_t, std::string>* spellNameFallback
) {
    (void)iconLoader;

    if (!isOpen_) return;

    const char* title = "Avoidance Breakdown";
    if (shouldOpenPopup_) {
        ImGui::OpenPopup(title);
        shouldOpenPopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(900, 650), ImGuiCond_FirstUseEver);

    if (ImGui::BeginPopupModal(title, &isOpen_, ImGuiWindowFlags_NoCollapse)) {
        // Header: target name + totals.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        ImGui::Text("%s", targetName_.empty() ? targetGuid_.c_str() : targetName_.c_str());
        ImGui::PopStyleColor();

        std::string totalAmountStr = ui::format::formatAmount(breakdown_.total_amount);
        ImGui::Text("Damage avoided: %s   Attacks avoided: %u",
                    totalAmountStr.c_str(), breakdown_.total_count);
        if (windowDuration_ms_ > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                "   (%.1fs window)",
                static_cast<float>(windowDuration_ms_) / 1000.0f);
        }
        ImGui::Separator();

        float childHeight = ImGui::GetContentRegionAvail().y - 40.0f;
        if (childHeight < 200.0f) childHeight = 200.0f;
        float halfWidth = ImGui::GetContentRegionAvail().x * 0.5f - 4.0f;

        // Left pane: enemies who attacked.
        ImGui::BeginChild("##sources", ImVec2(halfWidth, childHeight), true);
        ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "By Enemy");
        ImGui::Separator();

        if (breakdown_.by_source.empty()) {
            ImGui::TextDisabled("No attackers recorded.");
        } else {
            int64_t maxSourceAmount = std::max<int64_t>(1, breakdown_.by_source.front().amount);
            uint32_t maxSourceCount = std::max<uint32_t>(1, breakdown_.by_source.front().count);
            bool sourceUseAmount = breakdown_.by_source.front().amount > 0;

            for (const auto& s : breakdown_.by_source) {
                // Resolve display name via provided name first, then
                // the guidToName fallback maps like the meter does.
                std::string name = s.source_name;
                if (name.empty()) {
                    std::string_view gv(s.source_guid);
                    auto it = guidToName.find(gv);
                    if (it != guidToName.end()) name = std::string(it->second);
                    else if (combatGuidToName) {
                        auto it2 = combatGuidToName->find(s.source_guid);
                        if (it2 != combatGuidToName->end()) name = it2->second;
                    }
                }
                if (name.empty()) name = s.source_guid.substr(0, 16);

                float availableWidth = ImGui::GetContentRegionAvail().x;
                float ratio = sourceUseAmount
                    ? static_cast<float>(s.amount) / static_cast<float>(maxSourceAmount)
                    : static_cast<float>(s.count) / static_cast<float>(maxSourceCount);
                float barWidth = (availableWidth - 120.0f) * ratio;
                if (barWidth < 5.0f) barWidth = 5.0f;

                ImVec2 cursorPos = ImGui::GetCursorPos();
                ImVec2 screenPos = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    screenPos,
                    ImVec2(screenPos.x + barWidth, screenPos.y + 18.0f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.75f, 0.35f, 0.35f, 0.7f))
                );
                ImGui::SetCursorPos(ImVec2(cursorPos.x + 4.0f, cursorPos.y + 2.0f));
                ImGui::Text("%s", name.c_str());
                ImGui::SetCursorPos(ImVec2(cursorPos.x + availableWidth - 110.0f,
                                          cursorPos.y + 2.0f));
                if (s.amount > 0) {
                    std::string a = ui::format::formatAmount(s.amount);
                    ImGui::Text("%s (%u)", a.c_str(), s.count);
                } else {
                    ImGui::Text("%u", s.count);
                }
                ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + 20.0f));
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right pane: spells that were avoided.
        ImGui::BeginChild("##spells", ImVec2(halfWidth, childHeight), true);
        ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "By Spell");
        ImGui::Separator();

        if (breakdown_.by_spell.empty()) {
            ImGui::TextDisabled("No spells recorded.");
        } else {
            int64_t maxSpellAmount = std::max<int64_t>(1, breakdown_.by_spell.front().amount);
            uint32_t maxSpellCount = std::max<uint32_t>(1, breakdown_.by_spell.front().count);
            bool spellUseAmount = breakdown_.by_spell.front().amount > 0;

            for (const auto& sp : breakdown_.by_spell) {
                const char* name = resolveSpellName(sp.spell_id, sp.spell_name, spellNameFallback);
                std::string nameStr;
                if (!name || *name == '\0') {
                    if (sp.spell_id == 0 || sp.spell_id == 1) nameStr = L("meter.melee");
                    else nameStr = std::to_string(sp.spell_id);
                    name = nameStr.c_str();
                }

                float availableWidth = ImGui::GetContentRegionAvail().x;
                float ratio = spellUseAmount
                    ? static_cast<float>(sp.amount) / static_cast<float>(maxSpellAmount)
                    : static_cast<float>(sp.count) / static_cast<float>(maxSpellCount);
                float barWidth = (availableWidth - 120.0f) * ratio;
                if (barWidth < 5.0f) barWidth = 5.0f;

                ImVec2 cursorPos = ImGui::GetCursorPos();
                ImVec2 screenPos = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    screenPos,
                    ImVec2(screenPos.x + barWidth, screenPos.y + 18.0f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.55f, 0.75f, 0.7f))
                );
                ImGui::SetCursorPos(ImVec2(cursorPos.x + 4.0f, cursorPos.y + 2.0f));
                ImGui::Text("%s", name);
                ImGui::SetCursorPos(ImVec2(cursorPos.x + availableWidth - 110.0f,
                                          cursorPos.y + 2.0f));
                if (sp.amount > 0) {
                    std::string a = ui::format::formatAmount(sp.amount);
                    ImGui::Text("%s (%u)", a.c_str(), sp.count);
                } else {
                    ImGui::Text("%u", sp.count);
                }
                // Hover shows per-type split (e.g. "12 dodged, 4 absorbed 1.2M").
                // Each row needs a unique ID or ImGui warns about
                // conflicting IDs since we render many rows with the
                // same "##row" label.
                ImGui::SetCursorPos(cursorPos);
                ImGui::PushID(static_cast<int>(sp.spell_id));
                ImGui::InvisibleButton("##row", ImVec2(availableWidth, 18.0f));
                if (ImGui::IsItemHovered() && !sp.per_type.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", name);
                    ImGui::Separator();
                    for (const auto& t : sp.per_type) {
                        if (t.amount > 0) {
                            std::string a = ui::format::formatAmount(t.amount);
                            ImGui::Text("  %s: %u (%s)", missTypeLabel(t.type), t.count, a.c_str());
                        } else {
                            ImGui::Text("  %s: %u", missTypeLabel(t.type), t.count);
                        }
                    }
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
                ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + 20.0f));
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (awlui::Button("Close", awlui::ButtonVariant::Primary,
                          awlui::ButtonSize::Md, ImVec2(120, 0))) {
            close();
            ImGui::CloseCurrentPopup();
        }
        // Capture the actual laid-out size for OverlayApplication's
        // auto-grow.
        lastMeasuredSize_ = ImGui::GetWindowSize();
        ImGui::EndPopup();
    }

    // The user clicked the modal's [x] - keep close() in sync.
    if (!isOpen_) {
        close();
    }
}
