#include "UI/Panels/Settings/UIMobWeightPanel.h"
#include "UI/AwlUI/Widgets.h"
#include "Core/MobWeightSettings.h"
#include "Core/UnifiedSettings.h"
#include "Core/LocalizationManager.h"
#include "Core/StringInterner.h"
#include "CombatDatabase.h"
#include <imgui.h>
#include <algorithm>

UIMobWeightPanel::UIMobWeightPanel() = default;
UIMobWeightPanel::~UIMobWeightPanel() = default;

void UIMobWeightPanel::render(const CombatDatabase* combatDb,
                              const std::unordered_map<std::string, std::string>* guidToName) {
    if (!visible_) return;

    ImGui::SetNextWindowSize(ImVec2(500, 450), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(L("mobweight.title"), &visible_)) {
        auto& settings = MobWeightSettings::instance();

        if (awlui::Toggle(L("mobweight.enabled"), &settings.enabled)) {
            saveAndFlagChanged();
        }
        ImGui::TextDisabled("%s", L("mobweight.explainer"));
        ImGui::Spacing();

        // Manual refresh in case the same database got reloaded in place
        if (awlui::Button(L("mobweight.refresh"), awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
            cachedDb_ = nullptr;
        }

        // Rebuild the creature list only when the database changes
        if (combatDb != cachedDb_) {
            rebuildRows(combatDb, guidToName);
            cachedDb_ = combatDb;
        }

        // Creature list, leaving room for the reset button below
        float footerHeight = ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("MobWeightList", ImVec2(0, -footerHeight), true);

        if (rows_.empty()) {
            ImGui::TextDisabled("%s", L("empty.select_encounter_hint"));
        }
        for (const auto& row : rows_) {
            renderCreatureRow(row);
        }

        renderStaleEntries();

        ImGui::EndChild();

        // Bottom row: clear every saved weight
        if (awlui::Button(L("mobweight.reset_all"), awlui::ButtonVariant::Danger, awlui::ButtonSize::Sm)) {
            settings.weights.clear();
            saveAndFlagChanged();
        }

        // Laid-out size for the overlay's window auto-grow
        lastMeasuredSize_ = ImGui::GetWindowSize();
    }
    ImGui::End();
}

void UIMobWeightPanel::rebuildRows(const CombatDatabase* combatDb,
                                   const std::unordered_map<std::string, std::string>* guidToName) {
    rows_.clear();
    if (!combatDb) return;

    // Every damaged unit appears as a key in the damage-taken index.
    // Fold spawns of the same npc id into one row.
    const auto& petToOwner = combatDb->getPetToOwnerMap();
    std::unordered_map<uint32_t, CreatureRow> byNpcId;
    for (const auto& [guidId, records] : combatDb->getDamageTakenIndex()) {
        std::string_view guid = guidInterner().lookup(guidId);
        uint32_t npcId = MobWeightSettings::npcIdFromGuid(guid);
        if (npcId == 0) continue;  // players, pets without npc ids, etc.

        // Skip a player's own summons (a hunter pet, a DK's army). They
        // take damage and carry npc ids, but they're not mobs to weight.
        auto ownerIt = petToOwner.find(guidId);
        if (ownerIt != petToOwner.end() &&
            guidInterner().lookup(ownerIt->second).starts_with("Player-")) {
            continue;
        }

        auto& row = byNpcId[npcId];
        row.npcId = npcId;
        row.hitCount += records.size();
        if (row.name.empty() && guidToName) {
            auto it = guidToName->find(std::string(guid));
            if (it != guidToName->end()) {
                row.name = it->second;
            }
        }
    }

    rows_.reserve(byNpcId.size());
    for (auto& [npcId, row] : byNpcId) {
        if (row.name.empty()) {
            // No name in the log - show the npc id instead
            row.name = std::to_string(npcId);
        }
        rows_.push_back(std::move(row));
    }

    std::sort(rows_.begin(), rows_.end(), [](const CreatureRow& a, const CreatureRow& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.npcId < b.npcId;
    });
}

void UIMobWeightPanel::renderCreatureRow(const CreatureRow& row) {
    auto& settings = MobWeightSettings::instance();

    ImGui::PushID(static_cast<int>(row.npcId));

    ImGui::Text("%s", row.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%u)", row.npcId);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", LF("mobweight.hits_fmt", row.hitCount).c_str());

    // Slider works in percent; unlisted npcs count in full
    auto it = settings.weights.find(row.npcId);
    float percent = (it != settings.weights.end()) ? it->second * 100.0f : 100.0f;
    if (awlui::SliderFloat("", &percent, 0.0f, 100.0f, "%.0f%%")) {
        if (percent < 99.95f) {
            settings.weights[row.npcId] = percent / 100.0f;
        } else {
            // Back at 100% means full credit - drop the entry
            settings.weights.erase(row.npcId);
        }
        saveAndFlagChanged();
    }

    ImGui::Spacing();
    ImGui::PopID();
}

void UIMobWeightPanel::renderStaleEntries() {
    auto& settings = MobWeightSettings::instance();

    // Saved weights whose npc id isn't part of the current encounter,
    // listed so the user can clear leftovers from earlier fights
    std::vector<uint32_t> staleIds;
    for (const auto& [npcId, weight] : settings.weights) {
        bool inEncounter = std::any_of(rows_.begin(), rows_.end(),
            [npcId](const CreatureRow& row) { return row.npcId == npcId; });
        if (!inEncounter) {
            staleIds.push_back(npcId);
        }
    }
    if (staleIds.empty()) return;
    std::sort(staleIds.begin(), staleIds.end());

    ImGui::Separator();
    ImGui::TextDisabled("%s", L("mobweight.not_in_encounter"));

    for (uint32_t npcId : staleIds) {
        ImGui::PushID(static_cast<int>(npcId));
        if (awlui::IconButton("clear_weight", "x")) {
            settings.weights.erase(npcId);
            saveAndFlagChanged();
        }
        ImGui::SameLine();
        ImGui::Text("#%u", npcId);
        ImGui::SameLine();
        auto it = settings.weights.find(npcId);
        if (it != settings.weights.end()) {
            ImGui::TextDisabled("%.0f%%", it->second * 100.0f);
        }
        ImGui::PopID();
    }
}

void UIMobWeightPanel::saveAndFlagChanged() {
    auto& cache = SettingsCache::instance();
    cache.get().mobWeightSettingsJson = MobWeightSettings::instance().toJson();
    cache.markDirty();
    weightsChanged_ = true;
}
