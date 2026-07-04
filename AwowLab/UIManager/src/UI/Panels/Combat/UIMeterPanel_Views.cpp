// UIMeterPanel_Views.cpp
// Per-view render functions for the meter panel: damage, healing,
// damage taken, dispels, interrupts, overhealing, healing taken,
// friendly fire, CC breaks, deaths, avoidance, absorbs, and the
// enemy damage/healing views.

#include "UI/Panels/Combat/UIMeterPanel.h"
#include "UI/AwlUI/Widgets.h"
#include "Color/ActorColorGenerator.h"
#include "Core/NumberFormatter.h"
#include "Core/UIUtils.h"
#include "Core/LocalizationManager.h"
#include "DeathDatabase.h"
#include "imgui.h"
#include <algorithm>

void UIMeterPanel::renderDamageView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        awlui::EmptyState("meter_empty", "\xc2\xb7\xc2\xb7\xc2\xb7",
                          L("status.no_combat_data"), L("empty.select_encounter_hint"));
        return;
    }

    // Throttling: only refresh stats every REFRESH_INTERVAL_MS real-time OR on large time jumps
    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + 500) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + 500);
    bool timeModeSwitched = (config_.time_mode != cachedTimeMode_);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    bool needsRefresh = cachedCombatStats_.empty()
        || cachedViewType_ != MeterViewType::DamageDealt
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);

    if (needsRefresh) {
        // Use filter times if set (for segment filtering), otherwise use database bounds
        uint32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? static_cast<uint32_t>(filterStartTime_ms_)
            : combatDb->getMinTimestamp();
        uint32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? static_cast<uint32_t>(filterEndTime_ms_)
            : combatDb->getMaxTimestamp();
        cachedFilterStartTime_ms_ = filterStartTime_ms_;
        cachedFilterEndTime_ms_ = filterEndTime_ms_;
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        spellGroupSettings_.loadFromSettings();
        const auto& blacklist = spellGroupSettings_.getBlacklistedSpells();
        const std::unordered_set<uint32_t>* blacklistPtr = blacklist.empty() ? nullptr : &blacklist;

        cachedCombatStats_ = combatDb->getRankedByActorWithPets(
            CombatMetricType::DamageDealt, window.startTime, window.endTime, 100, false, blacklistPtr);
        cachedCombatStats_ = ui::filterFriendlyActors(std::move(cachedCombatStats_), colorGen, 40);

        cachedGrandTotal_ = 0;
        for (const auto& s : cachedCombatStats_) {
            cachedGrandTotal_ += s.effective_amount;
        }
        cachedDuration_ms_ = window.duration_ms;
        cachedWindowStart_ms_ = window.startTime;
        cachedWindowEnd_ms_ = window.endTime;
        cachedViewType_ = MeterViewType::DamageDealt;
        cachedTimeMode_ = config_.time_mode;
        cachedExpandedActorGuid_.clear();  // window shifted, drill-down needs to refetch

        lastRefreshTime_ms_ = now_ms;
        lastPlaybackTime_ms_ = currentTime_ms;
    }

    if (cachedCombatStats_.empty()) {
        awlui::EmptyState("meter_empty", "\xc2\xb7\xc2\xb7\xc2\xb7",
                          L("status.no_damage_data"), L("empty.scrub_hint"));
        return;
    }

    float grandAmountPerSecond = static_cast<float>(cachedGrandTotal_) / (static_cast<float>(cachedDuration_ms_) / 1000.0f);

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = "DamageChart##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
    renderCombatBarChart(cachedCombatStats_, colorGen, guidToName, cachedGrandTotal_, cachedDuration_ms_,
                         iconLoader, combatGuidToName, ImVec4(0.4f, 0.4f, 0.8f, 1.0f),
                         combatDb, CombatMetricType::DamageDealt,
                         cachedWindowStart_ms_, cachedWindowEnd_ms_);
    ImGui::EndChild();

    renderTotalSummary(cachedGrandTotal_, grandAmountPerSecond);
}

void UIMeterPanel::renderHealingView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        awlui::EmptyState("meter_empty", "\xc2\xb7\xc2\xb7\xc2\xb7",
                          L("status.no_combat_data"), L("empty.select_encounter_hint"));
        return;
    }

    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + 500) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + 500);
    bool timeModeSwitched = (config_.time_mode != cachedTimeMode_);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    bool needsRefresh = cachedCombatStats_.empty()
        || cachedViewType_ != MeterViewType::HealingDone
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);

    if (needsRefresh) {
        // Use filter times if set (for segment filtering), otherwise use database bounds
        uint32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? static_cast<uint32_t>(filterStartTime_ms_)
            : combatDb->getMinTimestamp();
        uint32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? static_cast<uint32_t>(filterEndTime_ms_)
            : combatDb->getMaxTimestamp();
        cachedFilterStartTime_ms_ = filterStartTime_ms_;
        cachedFilterEndTime_ms_ = filterEndTime_ms_;
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        spellGroupSettings_.loadFromSettings();
        const auto& blacklist = spellGroupSettings_.getBlacklistedSpells();
        const std::unordered_set<uint32_t>* blacklistPtr = blacklist.empty() ? nullptr : &blacklist;

        cachedCombatStats_ = combatDb->getRankedByActorWithPets(
            CombatMetricType::HealingDone, window.startTime, window.endTime, 100, false, blacklistPtr);
        cachedCombatStats_ = ui::filterFriendlyActors(std::move(cachedCombatStats_), colorGen, 40);

        cachedGrandTotal_ = 0;
        for (const auto& s : cachedCombatStats_) {
            cachedGrandTotal_ += s.effective_amount;
        }
        cachedDuration_ms_ = window.duration_ms;
        cachedWindowStart_ms_ = window.startTime;
        cachedWindowEnd_ms_ = window.endTime;
        cachedViewType_ = MeterViewType::HealingDone;
        cachedTimeMode_ = config_.time_mode;
        cachedExpandedActorGuid_.clear();

        lastRefreshTime_ms_ = now_ms;
        lastPlaybackTime_ms_ = currentTime_ms;
    }

    if (cachedCombatStats_.empty()) {
        awlui::EmptyState("meter_empty", "\xc2\xb7\xc2\xb7\xc2\xb7",
                          L("status.no_healing_data"), L("empty.scrub_hint"));
        return;
    }

    float grandAmountPerSecond = static_cast<float>(cachedGrandTotal_) / (static_cast<float>(cachedDuration_ms_) / 1000.0f);

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = "HealingChart##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
    renderCombatBarChart(cachedCombatStats_, colorGen, guidToName, cachedGrandTotal_, cachedDuration_ms_,
                         iconLoader, combatGuidToName, ImVec4(0.2f, 0.6f, 0.3f, 1.0f),
                         combatDb, CombatMetricType::HealingDone,
                         cachedWindowStart_ms_, cachedWindowEnd_ms_);
    ImGui::EndChild();

    renderTotalSummary(cachedGrandTotal_, grandAmountPerSecond);
}

void UIMeterPanel::renderDamageTakenView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        awlui::EmptyState("meter_empty", "\xc2\xb7\xc2\xb7\xc2\xb7",
                          L("status.no_combat_data"), L("empty.select_encounter_hint"));
        return;
    }

    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + 500) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + 500);
    bool timeModeSwitched = (config_.time_mode != cachedTimeMode_);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    bool needsRefresh = cachedCombatStats_.empty()
        || cachedViewType_ != MeterViewType::DamageTaken
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);

    if (needsRefresh) {
        // Use filter times if set (for segment filtering), otherwise use database bounds
        uint32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? static_cast<uint32_t>(filterStartTime_ms_)
            : combatDb->getMinTimestamp();
        uint32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? static_cast<uint32_t>(filterEndTime_ms_)
            : combatDb->getMaxTimestamp();
        cachedFilterStartTime_ms_ = filterStartTime_ms_;
        cachedFilterEndTime_ms_ = filterEndTime_ms_;
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        // Get damage taken with spell breakdown for ability view
        bool includeBreakdowns = (config_.view_type == MeterViewType::DamageTakenByAbility);
        cachedCombatStats_ = combatDb->getRankedByDamageTakenWithPets(
            window.startTime, window.endTime, 100, includeBreakdowns);

        // Filter to friendly actors (players)
        cachedCombatStats_ = ui::filterFriendlyActors(std::move(cachedCombatStats_), colorGen, 40);

        cachedGrandTotal_ = 0;
        for (const auto& s : cachedCombatStats_) {
            cachedGrandTotal_ += s.effective_amount;
        }
        cachedDuration_ms_ = window.duration_ms;
        cachedWindowStart_ms_ = window.startTime;
        cachedWindowEnd_ms_ = window.endTime;
        cachedViewType_ = MeterViewType::DamageTaken;
        cachedTimeMode_ = config_.time_mode;
        cachedExpandedActorGuid_.clear();

        lastRefreshTime_ms_ = now_ms;
        lastPlaybackTime_ms_ = currentTime_ms;
    }

    if (cachedCombatStats_.empty()) {
        awlui::EmptyState("meter_empty", "\xc2\xb7\xc2\xb7\xc2\xb7",
                          L("status.no_damage_taken_data"), L("empty.scrub_hint"));
        return;
    }

    float grandAmountPerSecond = static_cast<float>(cachedGrandTotal_) / (static_cast<float>(cachedDuration_ms_) / 1000.0f);

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = "DamageTakenChart##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
    renderCombatBarChart(cachedCombatStats_, colorGen, guidToName, cachedGrandTotal_, cachedDuration_ms_,
                         iconLoader, combatGuidToName, ImVec4(0.8f, 0.3f, 0.3f, 1.0f),
                         combatDb, CombatMetricType::DamageTaken,
                         cachedWindowStart_ms_, cachedWindowEnd_ms_);
    ImGui::EndChild();

    renderTotalSummary(cachedGrandTotal_, grandAmountPerSecond);
}

void UIMeterPanel::renderDamageTakenByView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        awlui::EmptyState("meter_empty", "\xc2\xb7\xc2\xb7\xc2\xb7",
                          L("status.no_combat_data"), L("empty.select_encounter_hint"));
        return;
    }

    // No target selected: show every enemy in the current segment as a
    // clickable picker. Clicking one seeds it as the target and drops
    // into the "who damaged X" view below. Mirrors Details' click-an-
    // enemy-to-filter flow so users don't need an external Enemy Grid
    // to pick from.
    if (selectedTargetGuid_.empty()) {
        uint32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? static_cast<uint32_t>(filterStartTime_ms_)
            : combatDb->getMinTimestamp();
        uint32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? static_cast<uint32_t>(filterEndTime_ms_)
            : combatDb->getMaxTimestamp();
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        auto enemies = combatDb->getRankedByEnemyDamage(window.startTime, window.endTime, 40);
        if (enemies.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", L("status.no_enemy_damage_data"));
            return;
        }

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", L("meter.select_target"));
        ImGui::Separator();

        int64_t maxAmount = enemies.front().effective_amount;
        if (maxAmount <= 0) maxAmount = 1;
        float availableWidth = ImGui::GetContentRegionAvail().x;

        for (size_t i = 0; i < enemies.size(); ++i) {
            const auto& e = enemies[i];

            std::string name;
            std::string_view gv(e.actor_guid);
            auto nameIt = guidToName.find(gv);
            if (nameIt != guidToName.end()) {
                name = std::string(nameIt->second);
            } else if (combatGuidToName) {
                auto it = combatGuidToName->find(e.actor_guid);
                if (it != combatGuidToName->end()) name = it->second;
            }
            if (name.empty()) name = e.actor_guid.substr(0, 16);
            if (ui::utf8CharCount(name) > 20) {
                name = ui::utf8Truncate(name, 18) + "..";
            }

            ImVec4 color(0.75f, 0.35f, 0.35f, 1.0f);
            if (colorGen) {
                ActorColor c = colorGen->getCachedColor(e.actor_guid);
                color = ImVec4(c.r, c.g, c.b, 1.0f);
            }

            float ratio = static_cast<float>(e.effective_amount) / static_cast<float>(maxAmount);
            float barWidth = (availableWidth - 140.0f) * ratio;
            if (barWidth < 5.0f) barWidth = 5.0f;

            ImGui::PushID(static_cast<int>(i * 10 + instanceId_) + 3000);
            ImVec2 cursorPos = ImGui::GetCursorPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetCursorScreenPos(),
                ImVec2(ImGui::GetCursorScreenPos().x + barWidth,
                       ImGui::GetCursorScreenPos().y + config_.bar_height),
                ImGui::ColorConvertFloat4ToU32(color)
            );
            ImGui::SetCursorPos(ImVec2(cursorPos.x + 5.0f,
                cursorPos.y + (config_.bar_height - ImGui::GetTextLineHeight()) / 2.0f));
            ImGui::Text("%s", name.c_str());

            std::string amountStr = ui::format::formatAmount(e.effective_amount);
            ImGui::SetCursorPos(ImVec2(cursorPos.x + availableWidth - 130.0f,
                cursorPos.y + (config_.bar_height - ImGui::GetTextLineHeight()) / 2.0f));
            ImGui::Text("%s", amountStr.c_str());

            ImGui::SetCursorPos(cursorPos);
            bool clicked = ImGui::InvisibleButton("##pick",
                ImVec2(availableWidth, config_.bar_height));
            if (clicked) {
                selectedTargetGuid_ = e.actor_guid;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Show who damaged %s", name.c_str());
            }

            ImGui::SetCursorPos(ImVec2(cursorPos.x,
                cursorPos.y + config_.bar_height + config_.bar_spacing));
            ImGui::PopID();
        }
        return;
    }

    // Resolve target name for display (use explicit string_view to avoid iterator issues)
    std::string targetName;
    std::string_view targetGuidView(selectedTargetGuid_);
    auto nameIt = guidToName.find(targetGuidView);
    if (nameIt != guidToName.end()) {
        targetName = std::string(nameIt->second);
    } else if (combatGuidToName) {
        auto combatNameIt = combatGuidToName->find(selectedTargetGuid_);
        if (combatNameIt != combatGuidToName->end()) {
            targetName = combatNameIt->second;
        }
    }
    if (targetName.empty()) {
        targetName = selectedTargetGuid_.substr(0, 16);
    }

    // Show target header with clear button
    ImGui::Text("%s: ", L("meter.damage_to"));
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", targetName.c_str());
    ImGui::SameLine();
    std::string clearBtnId = "X##clear" + std::to_string(instanceId_);
    if (awlui::Button(clearBtnId.c_str(), awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
        selectedTargetGuid_.clear();
        return;
    }
    ImGui::Separator();

    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + 500) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + 500);
    bool timeModeSwitched = (config_.time_mode != cachedTimeMode_);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    bool needsRefresh = cachedCombatStats_.empty()
        || cachedViewType_ != MeterViewType::DamageTakenBy
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);

    if (needsRefresh) {
        // Use filter times if set (for segment filtering), otherwise use database bounds
        uint32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? static_cast<uint32_t>(filterStartTime_ms_)
            : combatDb->getMinTimestamp();
        uint32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? static_cast<uint32_t>(filterEndTime_ms_)
            : combatDb->getMaxTimestamp();
        cachedFilterStartTime_ms_ = filterStartTime_ms_;
        cachedFilterEndTime_ms_ = filterEndTime_ms_;
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        cachedCombatStats_ = combatDb->getDamageDoneToTarget(
            selectedTargetGuid_, window.startTime, window.endTime, 100);

        cachedGrandTotal_ = 0;
        for (const auto& s : cachedCombatStats_) {
            cachedGrandTotal_ += s.effective_amount;
        }
        cachedDuration_ms_ = window.duration_ms;
        cachedViewType_ = MeterViewType::DamageTakenBy;
        cachedTimeMode_ = config_.time_mode;

        lastRefreshTime_ms_ = now_ms;
        lastPlaybackTime_ms_ = currentTime_ms;
    }

    if (cachedCombatStats_.empty()) {
        ImGui::Text("%s", L("status.no_damage_to_target"));
        return;
    }

    float grandAmountPerSecond = static_cast<float>(cachedGrandTotal_) / (static_cast<float>(cachedDuration_ms_) / 1000.0f);

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = "DamageTakenByChart##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
    renderCombatBarChart(cachedCombatStats_, colorGen, guidToName, cachedGrandTotal_, cachedDuration_ms_,
                         iconLoader, combatGuidToName, ImVec4(0.9f, 0.5f, 0.2f, 1.0f));  // Orange color
    ImGui::EndChild();

    renderTotalSummary(cachedGrandTotal_, grandAmountPerSecond);
}

void UIMeterPanel::renderDispelView(
    const DispelDatabase* dispelDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!dispelDb || dispelDb->empty()) {
        ImGui::Text("%s", L("status.no_dispel_data"));
        return;
    }

    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + 500) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + 500);
    bool timeModeSwitched = (config_.time_mode != cachedTimeMode_);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    bool needsRefresh = cachedDispelStats_.empty()
        || cachedViewType_ != MeterViewType::Dispels
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);

    if (needsRefresh) {
        // Use filter times if set (for segment filtering), otherwise use database bounds
        int32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? filterStartTime_ms_
            : dispelDb->getMinTimestamp();
        int32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? filterEndTime_ms_
            : dispelDb->getMaxTimestamp();
        cachedFilterStartTime_ms_ = filterStartTime_ms_;
        cachedFilterEndTime_ms_ = filterEndTime_ms_;
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        // Filter for dispels only (not interrupts)
        cachedDispelStats_ = dispelDb->getRankedByActor(window.startTime, window.endTime, 40, true,
                                                         DispelDatabase::FilterType::DispelsOnly);

        cachedDispelTotal_ = 0;
        for (const auto& s : cachedDispelStats_) {
            cachedDispelTotal_ += s.total_count;
        }
        cachedDuration_ms_ = window.duration_ms;
        cachedViewType_ = MeterViewType::Dispels;
        cachedTimeMode_ = config_.time_mode;

        lastRefreshTime_ms_ = now_ms;
        lastPlaybackTime_ms_ = currentTime_ms;
    }

    if (cachedDispelStats_.empty()) {
        ImGui::Text("%s", L("status.no_dispel_data"));
        return;
    }

    // Get dispel count for summary using same time window
    int32_t dbMinTime = (filterStartTime_ms_ > 0)
        ? filterStartTime_ms_
        : dispelDb->getMinTimestamp();
    int32_t dbMaxTime = dispelDb->getMaxTimestamp();
    bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
    ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);
    uint32_t dispelCount = dispelDb->getTotalDispelCount(window.startTime, window.endTime);

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = "DispelChart##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
    renderDispelBarChart(cachedDispelStats_, colorGen, guidToName, cachedDispelTotal_, iconLoader, combatGuidToName);
    ImGui::EndChild();

    // Show only dispel count (no interrupts in this tab)
    ImGui::Separator();
    ImGui::Text("%s: %u", L("meter.dispel_count"), dispelCount);
}

void UIMeterPanel::renderInterruptView(
    const DispelDatabase* dispelDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!dispelDb || dispelDb->empty()) {
        ImGui::Text("%s", L("status.no_interrupt_data"));
        return;
    }

    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + 500) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + 500);
    bool timeModeSwitched = (config_.time_mode != cachedTimeMode_);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    bool needsRefresh = cachedDispelStats_.empty()
        || cachedViewType_ != MeterViewType::Interrupts
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);

    if (needsRefresh) {
        // Use filter times if set (for segment filtering), otherwise use database bounds
        int32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? filterStartTime_ms_
            : dispelDb->getMinTimestamp();
        int32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? filterEndTime_ms_
            : dispelDb->getMaxTimestamp();
        cachedFilterStartTime_ms_ = filterStartTime_ms_;
        cachedFilterEndTime_ms_ = filterEndTime_ms_;
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        // Filter for interrupts only
        cachedDispelStats_ = dispelDb->getRankedByActor(window.startTime, window.endTime, 40, true,
                                                         DispelDatabase::FilterType::InterruptsOnly);

        cachedDispelTotal_ = 0;
        for (const auto& s : cachedDispelStats_) {
            cachedDispelTotal_ += s.total_count;
        }
        cachedDuration_ms_ = window.duration_ms;
        cachedViewType_ = MeterViewType::Interrupts;
        cachedTimeMode_ = config_.time_mode;

        lastRefreshTime_ms_ = now_ms;
        lastPlaybackTime_ms_ = currentTime_ms;
    }

    if (cachedDispelStats_.empty()) {
        ImGui::Text("%s", L("status.no_interrupt_data"));
        return;
    }

    // Get interrupt count for summary using same time window
    int32_t dbMinTime = (filterStartTime_ms_ > 0)
        ? filterStartTime_ms_
        : dispelDb->getMinTimestamp();
    int32_t dbMaxTime = dispelDb->getMaxTimestamp();
    bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
    ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);
    uint32_t interruptCount = dispelDb->getTotalInterruptCount(window.startTime, window.endTime);

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = "InterruptChart##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
    renderDispelBarChart(cachedDispelStats_, colorGen, guidToName, cachedDispelTotal_, iconLoader, combatGuidToName);
    ImGui::EndChild();

    // Show only interrupt count
    ImGui::Separator();
    ImGui::Text("%s: %u", L("meter.interrupt_count"), interruptCount);
}

void UIMeterPanel::renderOverhealingView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        awlui::EmptyState("meter_empty", "\xc2\xb7\xc2\xb7\xc2\xb7",
                          L("status.no_combat_data"), L("empty.select_encounter_hint"));
        return;
    }

    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + 500) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + 500);
    bool timeModeSwitched = (config_.time_mode != cachedTimeMode_);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    bool needsRefresh = cachedCombatStats_.empty()
        || cachedViewType_ != MeterViewType::Overhealing
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);

    if (needsRefresh) {
        // Use filter times if set (for segment filtering), otherwise use database bounds
        uint32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? static_cast<uint32_t>(filterStartTime_ms_)
            : combatDb->getMinTimestamp();
        uint32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? static_cast<uint32_t>(filterEndTime_ms_)
            : combatDb->getMaxTimestamp();
        cachedFilterStartTime_ms_ = filterStartTime_ms_;
        cachedFilterEndTime_ms_ = filterEndTime_ms_;
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        cachedCombatStats_ = combatDb->getRankedByOverhealing(window.startTime, window.endTime, 100);
        cachedCombatStats_ = ui::filterFriendlyActors(std::move(cachedCombatStats_), colorGen, 40);

        cachedGrandTotal_ = 0;
        for (const auto& s : cachedCombatStats_) {
            cachedGrandTotal_ += s.effective_amount;  // effective_amount contains overheal
        }
        cachedDuration_ms_ = window.duration_ms;
        cachedViewType_ = MeterViewType::Overhealing;
        cachedTimeMode_ = config_.time_mode;

        lastRefreshTime_ms_ = now_ms;
        lastPlaybackTime_ms_ = currentTime_ms;
    }

    if (cachedCombatStats_.empty()) {
        ImGui::Text("%s", L("status.no_overhealing_data"));
        return;
    }

    float grandAmountPerSecond = static_cast<float>(cachedGrandTotal_) / (static_cast<float>(cachedDuration_ms_) / 1000.0f);

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = "OverhealingChart##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
    renderCombatBarChart(cachedCombatStats_, colorGen, guidToName, cachedGrandTotal_, cachedDuration_ms_,
                         iconLoader, combatGuidToName, ImVec4(0.8f, 0.6f, 0.2f, 1.0f));  // Orange for overheal
    ImGui::EndChild();

    renderTotalSummary(cachedGrandTotal_, grandAmountPerSecond);
}

void UIMeterPanel::renderHealingTakenView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        awlui::EmptyState("meter_empty", "\xc2\xb7\xc2\xb7\xc2\xb7",
                          L("status.no_combat_data"), L("empty.select_encounter_hint"));
        return;
    }

    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + 500) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + 500);
    bool timeModeSwitched = (config_.time_mode != cachedTimeMode_);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    bool needsRefresh = cachedCombatStats_.empty()
        || cachedViewType_ != MeterViewType::HealingTaken
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);

    if (needsRefresh) {
        // Use filter times if set (for segment filtering), otherwise use database bounds
        uint32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? static_cast<uint32_t>(filterStartTime_ms_)
            : combatDb->getMinTimestamp();
        uint32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? static_cast<uint32_t>(filterEndTime_ms_)
            : combatDb->getMaxTimestamp();
        cachedFilterStartTime_ms_ = filterStartTime_ms_;
        cachedFilterEndTime_ms_ = filterEndTime_ms_;
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        cachedCombatStats_ = combatDb->getRankedByHealingTaken(window.startTime, window.endTime, 100);
        cachedCombatStats_ = ui::filterFriendlyActors(std::move(cachedCombatStats_), colorGen, 40);

        cachedGrandTotal_ = 0;
        for (const auto& s : cachedCombatStats_) {
            cachedGrandTotal_ += s.effective_amount;
        }
        cachedDuration_ms_ = window.duration_ms;
        cachedWindowStart_ms_ = window.startTime;
        cachedWindowEnd_ms_ = window.endTime;
        cachedViewType_ = MeterViewType::HealingTaken;
        cachedTimeMode_ = config_.time_mode;
        cachedExpandedActorGuid_.clear();

        lastRefreshTime_ms_ = now_ms;
        lastPlaybackTime_ms_ = currentTime_ms;
    }

    if (cachedCombatStats_.empty()) {
        ImGui::Text("%s", L("status.no_healing_taken_data"));
        return;
    }

    float grandAmountPerSecond = static_cast<float>(cachedGrandTotal_) / (static_cast<float>(cachedDuration_ms_) / 1000.0f);

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = "HealingTakenChart##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
    renderCombatBarChart(cachedCombatStats_, colorGen, guidToName, cachedGrandTotal_, cachedDuration_ms_,
                         iconLoader, combatGuidToName, ImVec4(0.3f, 0.7f, 0.4f, 1.0f),
                         combatDb, CombatMetricType::HealingReceived,
                         cachedWindowStart_ms_, cachedWindowEnd_ms_);
    ImGui::EndChild();

    renderTotalSummary(cachedGrandTotal_, grandAmountPerSecond);
}

void UIMeterPanel::renderFriendlyFireView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (!combatDb || combatDb->empty()) {
        awlui::EmptyState("meter_empty", "\xc2\xb7\xc2\xb7\xc2\xb7",
                          L("status.no_combat_data"), L("empty.select_encounter_hint"));
        return;
    }

    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + 500) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + 500);
    bool timeModeSwitched = (config_.time_mode != cachedTimeMode_);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    bool needsRefresh = cachedCombatStats_.empty()
        || cachedViewType_ != MeterViewType::FriendlyFire
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);

    if (needsRefresh) {
        // Use filter times if set (for segment filtering), otherwise use database bounds
        uint32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? static_cast<uint32_t>(filterStartTime_ms_)
            : combatDb->getMinTimestamp();
        uint32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? static_cast<uint32_t>(filterEndTime_ms_)
            : combatDb->getMaxTimestamp();
        cachedFilterStartTime_ms_ = filterStartTime_ms_;
        cachedFilterEndTime_ms_ = filterEndTime_ms_;
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        cachedCombatStats_ = combatDb->getRankedByFriendlyFire(window.startTime, window.endTime, 100);

        cachedGrandTotal_ = 0;
        for (const auto& s : cachedCombatStats_) {
            cachedGrandTotal_ += s.effective_amount;
        }
        cachedDuration_ms_ = window.duration_ms;
        cachedWindowStart_ms_ = window.startTime;
        cachedWindowEnd_ms_ = window.endTime;
        cachedViewType_ = MeterViewType::FriendlyFire;
        cachedTimeMode_ = config_.time_mode;
        cachedExpandedActorGuid_.clear();

        lastRefreshTime_ms_ = now_ms;
        lastPlaybackTime_ms_ = currentTime_ms;
    }

    if (cachedCombatStats_.empty()) {
        ImGui::Text("%s", L("status.no_friendly_fire_data"));
        return;
    }

    float grandAmountPerSecond = static_cast<float>(cachedGrandTotal_) / (static_cast<float>(cachedDuration_ms_) / 1000.0f);

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = "FriendlyFireChart##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
    renderCombatBarChart(cachedCombatStats_, colorGen, guidToName, cachedGrandTotal_, cachedDuration_ms_,
                         iconLoader, combatGuidToName, ImVec4(0.9f, 0.3f, 0.1f, 1.0f),
                         combatDb, CombatMetricType::FriendlyFire,
                         cachedWindowStart_ms_, cachedWindowEnd_ms_);
    ImGui::EndChild();

    renderTotalSummary(cachedGrandTotal_, grandAmountPerSecond);
}

void UIMeterPanel::renderCCBreaksView(
    const AuraDatabase* auraDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    (void)iconLoader;  // Reserved for future use

    if (!auraDb || !auraDb->hasCCBreaks()) {
        ImGui::Text("%s", L("status.no_cc_break_data"));
        return;
    }

    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + 500) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + 500);
    bool timeModeSwitched = (config_.time_mode != cachedTimeMode_);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    bool needsRefresh = cachedCCBreakStats_.empty()
        || cachedViewType_ != MeterViewType::CCBreaks
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);

    if (needsRefresh) {
        // Use filter times if set (for segment filtering), otherwise use database bounds
        int32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? filterStartTime_ms_
            : auraDb->getMinTimestamp();
        int32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? filterEndTime_ms_
            : auraDb->getMaxTimestamp();
        cachedFilterStartTime_ms_ = filterStartTime_ms_;
        cachedFilterEndTime_ms_ = filterEndTime_ms_;
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        cachedCCBreakStats_ = auraDb->getRankedByCCBreaks(window.startTime, window.endTime, 40);

        cachedCCBreakTotal_ = 0;
        for (const auto& s : cachedCCBreakStats_) {
            cachedCCBreakTotal_ += s.break_count;
        }
        cachedDuration_ms_ = window.duration_ms;
        cachedViewType_ = MeterViewType::CCBreaks;
        cachedTimeMode_ = config_.time_mode;

        lastRefreshTime_ms_ = now_ms;
        lastPlaybackTime_ms_ = currentTime_ms;
    }

    if (cachedCCBreakStats_.empty()) {
        ImGui::Text("%s", L("status.no_cc_break_data"));
        return;
    }

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = "CCBreaksChart##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);

    // Render CC breaks bar chart
    float availableWidth = ImGui::GetContentRegionAvail().x;
    uint32_t maxCount = cachedCCBreakStats_.empty() ? 1 : cachedCCBreakStats_[0].break_count;
    if (maxCount == 0) maxCount = 1;

    for (size_t i = 0; i < cachedCCBreakStats_.size(); ++i) {
        const auto& actorStats = cachedCCBreakStats_[i];

        // Resolve actor name (use explicit string_view to avoid iterator issues)
        std::string actorName;
        std::string_view actorGuidView(actorStats.actor_guid);
        auto nameIt = guidToName.find(actorGuidView);
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

        // Truncate name if too long
        if (ui::utf8CharCount(actorName) > 12) {
            actorName = ui::utf8Truncate(actorName, 10) + "..";
        }

        // Get actor color
        ImVec4 barColor(0.7f, 0.3f, 0.3f, 1.0f);  // Red for CC breaks
        if (colorGen) {
            ActorColor color = colorGen->getCachedColor(actorStats.actor_guid);
            barColor = ImVec4(color.r, color.g, color.b, 1.0f);
        }

        // Calculate bar width
        float barWidth = (availableWidth - 160.0f) * (static_cast<float>(actorStats.break_count) / maxCount);
        if (barWidth < 5.0f) barWidth = 5.0f;

        // Render bar
        ImGui::PushID(static_cast<int>(i * 10 + instanceId_));

        ImVec2 cursorPos = ImGui::GetCursorPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetCursorScreenPos(),
            ImVec2(ImGui::GetCursorScreenPos().x + barWidth, ImGui::GetCursorScreenPos().y + config_.bar_height),
            ImGui::ColorConvertFloat4ToU32(barColor)
        );

        // Actor name on bar
        ImGui::SetCursorPos(ImVec2(cursorPos.x + 5.0f, cursorPos.y + (config_.bar_height - ImGui::GetTextLineHeight()) / 2.0f));
        ImGui::Text("%s", actorName.c_str());

        // Count on the right
        ImGui::SetCursorPos(ImVec2(cursorPos.x + availableWidth - 150.0f, cursorPos.y + (config_.bar_height - ImGui::GetTextLineHeight()) / 2.0f));
        float percent = cachedCCBreakTotal_ > 0 ? (static_cast<float>(actorStats.break_count) / cachedCCBreakTotal_ * 100.0f) : 0.0f;
        ImGui::Text("%u (%.1f%%)", actorStats.break_count, percent);

        // Handle click
        ImGui::SetCursorPos(cursorPos);
        if (ImGui::InvisibleButton("##bar", ImVec2(availableWidth, config_.bar_height))) {
            clickedActor_ = actorStats.actor_guid;
        }

        // Tooltip with breakdown
        if (ImGui::IsItemHovered() && !actorStats.breakdown.empty()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s - %s", actorName.c_str(), L("meter.cc_breaks"));
            ImGui::Separator();
            for (const auto& detail : actorStats.breakdown) {
                ImGui::Text("  %s x%u", detail.cc_spell_name.c_str(), detail.count);
            }
            ImGui::EndTooltip();
        }

        ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + config_.bar_height + config_.bar_spacing));
        ImGui::PopID();
    }

    ImGui::EndChild();

    // Summary
    ImGui::Separator();
    ImGui::Text("%s: %u", L("meter.total_cc_breaks"), cachedCCBreakTotal_);
}

void UIMeterPanel::renderDeathsView(
    const DeathDatabase* deathDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    (void)iconLoader;  // Reserved for future use

    if (!deathDb) {
        ImGui::Text("%s", L("status.no_death_data"));
        return;
    }

    // Throttling: only refresh stats on config changes or time jumps
    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + 500) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + 500);
    bool timeModeSwitched = (config_.time_mode != cachedTimeMode_);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    bool needsRefresh = cachedDeathEvents_.empty()
        || cachedViewType_ != MeterViewType::Deaths
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);

    if (needsRefresh) {
        // Use filter times if set (for segment filtering), otherwise use database bounds
        cachedFilterStartTime_ms_ = filterStartTime_ms_;
        cachedFilterEndTime_ms_ = filterEndTime_ms_;
        int32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? filterStartTime_ms_
            : deathDb->getMinTimestamp();
        int32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? filterEndTime_ms_
            : deathDb->getMaxTimestamp();
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        // Get deaths in filtered time range
        cachedDeathEvents_ = deathDb->getPlayerDeathsInRange(window.startTime, window.endTime);
        cachedDeathTotal_ = static_cast<uint32_t>(cachedDeathEvents_.size());
        cachedDuration_ms_ = window.duration_ms;
        cachedViewType_ = MeterViewType::Deaths;
        cachedTimeMode_ = config_.time_mode;

        lastRefreshTime_ms_ = now_ms;
        lastPlaybackTime_ms_ = currentTime_ms;
    }

    if (cachedDeathEvents_.empty()) {
        ImGui::Text("%s", L("status.no_death_data"));
        return;
    }

    // Use cached deaths for rendering
    const auto& playerDeaths = cachedDeathEvents_;

    // Aggregate deaths by actor
    struct DeathCount {
        std::string actor_guid;
        uint32_t count = 0;
    };
    std::unordered_map<std::string, uint32_t> deathCountMap;
    for (const auto* death : playerDeaths) {
        deathCountMap[death->actor_guid]++;
    }

    // Convert to sorted vector
    std::vector<DeathCount> deathCounts;
    deathCounts.reserve(deathCountMap.size());
    for (const auto& [guid, count] : deathCountMap) {
        deathCounts.push_back({guid, count});
    }
    std::sort(deathCounts.begin(), deathCounts.end(),
        [](const DeathCount& a, const DeathCount& b) { return a.count > b.count; });

    uint32_t totalDeaths = cachedDeathTotal_;

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = "DeathsChart##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);

    // Render deaths bar chart
    float availableWidth = ImGui::GetContentRegionAvail().x;
    uint32_t maxCount = deathCounts.empty() ? 1 : deathCounts[0].count;
    if (maxCount == 0) maxCount = 1;

    for (size_t i = 0; i < deathCounts.size(); ++i) {
        const auto& actorStats = deathCounts[i];

        // Resolve actor name (use explicit string_view to avoid iterator issues)
        std::string actorName;
        std::string_view actorGuidView(actorStats.actor_guid);
        auto nameIt = guidToName.find(actorGuidView);
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

        // Truncate name if too long
        if (ui::utf8CharCount(actorName) > 12) {
            actorName = ui::utf8Truncate(actorName, 10) + "..";
        }

        // Get actor color
        ImVec4 barColor(0.5f, 0.1f, 0.1f, 1.0f);  // Dark red for deaths
        if (colorGen) {
            ActorColor color = colorGen->getCachedColor(actorStats.actor_guid);
            barColor = ImVec4(color.r, color.g, color.b, 1.0f);
        }

        // Calculate bar width
        float barWidth = (availableWidth - 160.0f) * (static_cast<float>(actorStats.count) / maxCount);
        if (barWidth < 5.0f) barWidth = 5.0f;

        // Render bar
        ImGui::PushID(static_cast<int>(i * 10 + instanceId_));

        ImVec2 cursorPos = ImGui::GetCursorPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetCursorScreenPos(),
            ImVec2(ImGui::GetCursorScreenPos().x + barWidth, ImGui::GetCursorScreenPos().y + config_.bar_height),
            ImGui::ColorConvertFloat4ToU32(barColor)
        );

        // Actor name on bar
        ImGui::SetCursorPos(ImVec2(cursorPos.x + 5.0f, cursorPos.y + (config_.bar_height - ImGui::GetTextLineHeight()) / 2.0f));
        ImGui::Text("%s", actorName.c_str());

        // Count on the right
        ImGui::SetCursorPos(ImVec2(cursorPos.x + availableWidth - 150.0f, cursorPos.y + (config_.bar_height - ImGui::GetTextLineHeight()) / 2.0f));
        float percent = totalDeaths > 0 ? (static_cast<float>(actorStats.count) / totalDeaths * 100.0f) : 0.0f;
        ImGui::Text("%u (%.1f%%)", actorStats.count, percent);

        // Handle click - could open death recap in the future
        ImGui::SetCursorPos(cursorPos);
        if (ImGui::InvisibleButton("##bar", ImVec2(availableWidth, config_.bar_height))) {
            clickedActor_ = actorStats.actor_guid;
        }

        ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + config_.bar_height + config_.bar_spacing));
        ImGui::PopID();
    }

    ImGui::EndChild();

    // Summary
    ImGui::Separator();
    ImGui::Text("%s: %u", L("meter.total_deaths"), totalDeaths);
}

void UIMeterPanel::renderAvoidanceView(
    const AvoidanceDatabase* avoidanceDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    (void)iconLoader;  // Reserved for future use

    if (!avoidanceDb || avoidanceDb->empty()) {
        ImGui::Text("%s", L("status.no_avoidance_data"));
        return;
    }

    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + 500) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + 500);
    bool timeModeSwitched = (config_.time_mode != cachedTimeMode_);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    bool needsRefresh = cachedAvoidanceStats_.empty()
        || cachedViewType_ != MeterViewType::Avoidance
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);

    if (needsRefresh) {
        // Use filter times if set (for segment filtering), otherwise use database bounds
        int32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? filterStartTime_ms_
            : avoidanceDb->getMinTimestamp();
        int32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? filterEndTime_ms_
            : avoidanceDb->getMaxTimestamp();
        cachedFilterStartTime_ms_ = filterStartTime_ms_;
        cachedFilterEndTime_ms_ = filterEndTime_ms_;
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        cachedAvoidanceStats_ = avoidanceDb->getRankedByTarget(window.startTime, window.endTime, 40, true);

        cachedAvoidanceTotal_ = 0;
        cachedAvoidanceAmount_ = 0;
        for (const auto& s : cachedAvoidanceStats_) {
            cachedAvoidanceTotal_ += s.total_count;
            cachedAvoidanceAmount_ += s.total_amount;
        }
        cachedDuration_ms_ = window.duration_ms;
        cachedViewType_ = MeterViewType::Avoidance;
        cachedTimeMode_ = config_.time_mode;

        lastRefreshTime_ms_ = now_ms;
        lastPlaybackTime_ms_ = currentTime_ms;
    }

    if (cachedAvoidanceStats_.empty()) {
        ImGui::Text("%s", L("status.no_avoidance_data"));
        return;
    }

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = "AvoidanceChart##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);

    // Bar length: prefer total_amount when any absorb/block/resist
    // amount was seen in this window; fall back to count for pure-
    // dodge fights so the meter isn't all zero-length bars.
    float availableWidth = ImGui::GetContentRegionAvail().x;
    bool useAmountBars = cachedAvoidanceAmount_ > 0;
    int64_t maxAmount = 1;
    uint32_t maxCount = 1;
    if (!cachedAvoidanceStats_.empty()) {
        maxAmount = std::max<int64_t>(1, cachedAvoidanceStats_.front().total_amount);
        maxCount = std::max<uint32_t>(1, cachedAvoidanceStats_.front().total_count);
    }

    auto missTypeLabel = [](MissType type) -> const char* {
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
    };

    for (size_t i = 0; i < cachedAvoidanceStats_.size(); ++i) {
        const auto& actorStats = cachedAvoidanceStats_[i];

        // Resolve actor name (use explicit string_view to avoid iterator issues)
        std::string actorName;
        std::string_view actorGuidView(actorStats.actor_guid);
        auto nameIt = guidToName.find(actorGuidView);
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

        // Truncate name if too long
        if (ui::utf8CharCount(actorName) > 12) {
            actorName = ui::utf8Truncate(actorName, 10) + "..";
        }

        // Get actor color
        ImVec4 barColor(0.3f, 0.5f, 0.7f, 1.0f);  // Blue for avoidance
        if (colorGen) {
            ActorColor color = colorGen->getCachedColor(actorStats.actor_guid);
            barColor = ImVec4(color.r, color.g, color.b, 1.0f);
        }

        // Bar width scales to the primary metric for this window.
        float ratio;
        if (useAmountBars) {
            ratio = static_cast<float>(actorStats.total_amount) / static_cast<float>(maxAmount);
        } else {
            ratio = static_cast<float>(actorStats.total_count) / static_cast<float>(maxCount);
        }
        float barWidth = (availableWidth - 200.0f) * ratio;
        if (barWidth < 5.0f) barWidth = 5.0f;

        ImGui::PushID(static_cast<int>(i * 10 + instanceId_));

        ImVec2 cursorPos = ImGui::GetCursorPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetCursorScreenPos(),
            ImVec2(ImGui::GetCursorScreenPos().x + barWidth, ImGui::GetCursorScreenPos().y + config_.bar_height),
            ImGui::ColorConvertFloat4ToU32(barColor)
        );

        // Actor name on bar
        ImGui::SetCursorPos(ImVec2(cursorPos.x + 5.0f, cursorPos.y + (config_.bar_height - ImGui::GetTextLineHeight()) / 2.0f));
        ImGui::Text("%s", actorName.c_str());

        // Right-side label: "12.3M (145)" when amounts are present,
        // just "145" when only counts are available.
        ImGui::SetCursorPos(ImVec2(cursorPos.x + availableWidth - 190.0f,
            cursorPos.y + (config_.bar_height - ImGui::GetTextLineHeight()) / 2.0f));
        if (actorStats.total_amount > 0) {
            std::string amountStr = ui::format::formatAmount(actorStats.total_amount);
            ImGui::Text("%s (%u)", amountStr.c_str(), actorStats.total_count);
        } else {
            ImGui::Text("%u", actorStats.total_count);
        }

        // Handle click
        ImGui::SetCursorPos(cursorPos);
        if (ImGui::InvisibleButton("##bar", ImVec2(availableWidth, config_.bar_height))) {
            clickedActor_ = actorStats.actor_guid;
        }

        // Tooltip with per-type breakdown. Uses the breakdown vector
        // that AvoidanceDatabase already sorts by amount desc so the
        // most impactful mitigation type shows first.
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s - %s", actorName.c_str(), L("meter.avoidance"));
            if (actorStats.total_amount > 0) {
                std::string amountStr = ui::format::formatAmount(actorStats.total_amount);
                ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f),
                    "Damage avoided: %s", amountStr.c_str());
            }
            ImGui::Separator();
            for (const auto& b : actorStats.breakdown) {
                if (b.amount > 0) {
                    std::string a = ui::format::formatAmount(b.amount);
                    ImGui::Text("  %s: %u  (%s)", missTypeLabel(b.type), b.count, a.c_str());
                } else {
                    ImGui::Text("  %s: %u", missTypeLabel(b.type), b.count);
                }
            }
            ImGui::EndTooltip();
        }

        ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + config_.bar_height + config_.bar_spacing));
        ImGui::PopID();
    }

    ImGui::EndChild();

    // Summary line shows amount when available, otherwise the count.
    ImGui::Separator();
    if (cachedAvoidanceAmount_ > 0) {
        std::string totalStr = ui::format::formatAmount(cachedAvoidanceAmount_);
        ImGui::Text("%s: %s  (%u)", L("meter.total_avoidance"),
                    totalStr.c_str(), cachedAvoidanceTotal_);
    } else {
        ImGui::Text("%s: %u", L("meter.total_avoidance"), cachedAvoidanceTotal_);
    }
}

void UIMeterPanel::renderAbsorbsView(
    const AbsorbDatabase* absorbDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    (void)iconLoader;

    if (!absorbDb || absorbDb->empty()) {
        ImGui::Text("%s", L("status.no_absorb_data"));
        return;
    }

    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms_ + 500) ||
                        (lastPlaybackTime_ms_ > currentTime_ms + 500);
    bool timeModeSwitched = (config_.time_mode != cachedTimeMode_);
    bool filterChanged = (filterStartTime_ms_ != cachedFilterStartTime_ms_)
                      || (filterEndTime_ms_ != cachedFilterEndTime_ms_);
    bool needsRefresh = cachedAbsorbStats_.empty()
        || cachedViewType_ != MeterViewType::Absorbs
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms_ >= REFRESH_INTERVAL_MS);

    if (needsRefresh) {
        int32_t dbMinTime = (filterStartTime_ms_ > 0)
            ? filterStartTime_ms_
            : absorbDb->getMinTimestamp();
        int32_t dbMaxTime = (filterEndTime_ms_ > 0)
            ? filterEndTime_ms_
            : absorbDb->getMaxTimestamp();
        cachedFilterStartTime_ms_ = filterStartTime_ms_;
        cachedFilterEndTime_ms_ = filterEndTime_ms_;
        bool useFullEncounter = (config_.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        cachedAbsorbStats_ = absorbDb->getRankedByAbsorber(window.startTime, window.endTime, 40, true);
        cachedAbsorbTotal_ = 0;
        for (const auto& s : cachedAbsorbStats_) {
            cachedAbsorbTotal_ += s.total_absorbed;
        }
        cachedDuration_ms_ = window.duration_ms;
        cachedViewType_ = MeterViewType::Absorbs;
        cachedTimeMode_ = config_.time_mode;
        lastRefreshTime_ms_ = now_ms;
        lastPlaybackTime_ms_ = currentTime_ms;
    }

    if (cachedAbsorbStats_.empty()) {
        ImGui::Text("%s", L("status.no_absorb_data"));
        return;
    }

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = "AbsorbChart##" + std::to_string(instanceId_);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);

    float availableWidth = ImGui::GetContentRegionAvail().x;
    int64_t maxAmount = cachedAbsorbStats_.front().total_absorbed;
    if (maxAmount <= 0) maxAmount = 1;

    float durationSeconds = cachedDuration_ms_ > 0
        ? static_cast<float>(cachedDuration_ms_) / 1000.0f
        : 0.0f;

    for (size_t i = 0; i < cachedAbsorbStats_.size(); ++i) {
        const auto& absorbStats = cachedAbsorbStats_[i];

        std::string absorberName;
        std::string_view guidView(absorbStats.actor_guid);
        auto nameIt = guidToName.find(guidView);
        if (nameIt != guidToName.end()) {
            absorberName = std::string(nameIt->second);
        } else if (combatGuidToName) {
            auto combatNameIt = combatGuidToName->find(absorbStats.actor_guid);
            if (combatNameIt != combatGuidToName->end()) {
                absorberName = combatNameIt->second;
            }
        }
        if (absorberName.empty()) {
            absorberName = absorbStats.actor_guid.substr(0, 12);
        }
        if (ui::utf8CharCount(absorberName) > 14) {
            absorberName = ui::utf8Truncate(absorberName, 12) + "..";
        }

        ImVec4 barColor(0.35f, 0.65f, 0.85f, 1.0f);
        if (colorGen) {
            ActorColor color = colorGen->getCachedColor(absorbStats.actor_guid);
            barColor = ImVec4(color.r, color.g, color.b, 1.0f);
        }

        float ratio = static_cast<float>(absorbStats.total_absorbed) / static_cast<float>(maxAmount);
        float barWidth = (availableWidth - 180.0f) * ratio;
        if (barWidth < 5.0f) barWidth = 5.0f;

        ImGui::PushID(static_cast<int>(i * 10 + instanceId_));

        ImVec2 cursorPos = ImGui::GetCursorPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetCursorScreenPos(),
            ImVec2(ImGui::GetCursorScreenPos().x + barWidth,
                   ImGui::GetCursorScreenPos().y + config_.bar_height),
            ImGui::ColorConvertFloat4ToU32(barColor)
        );

        ImGui::SetCursorPos(ImVec2(cursorPos.x + 5.0f,
            cursorPos.y + (config_.bar_height - ImGui::GetTextLineHeight()) / 2.0f));
        ImGui::Text("%s", absorberName.c_str());

        std::string amountStr = ui::format::formatAmount(absorbStats.total_absorbed);
        float perSecond = durationSeconds > 0.0f
            ? static_cast<float>(absorbStats.total_absorbed) / durationSeconds
            : 0.0f;
        std::string apsStr = ui::format::formatPerSecond(perSecond);

        ImGui::SetCursorPos(ImVec2(cursorPos.x + availableWidth - 170.0f,
            cursorPos.y + (config_.bar_height - ImGui::GetTextLineHeight()) / 2.0f));
        ImGui::Text("%s (%s, %.1f%%)", amountStr.c_str(), apsStr.c_str(),
                    absorbStats.percent_of_total);

        ImGui::SetCursorPos(cursorPos);
        if (ImGui::InvisibleButton("##bar", ImVec2(availableWidth, config_.bar_height))) {
            clickedActor_ = absorbStats.actor_guid;
        }

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s - %s", absorberName.c_str(), L("meter.tab.absorbs"));
            ImGui::Separator();
            ImGui::Text("%s: %s", L("meter.total_absorbed"), amountStr.c_str());
            ImGui::Text("%s: %u", L("meter.event_count"), absorbStats.event_count);
            if (!absorbStats.spell_breakdown.empty()) {
                ImGui::Separator();
                for (const auto& sb : absorbStats.spell_breakdown) {
                    const char* spellName = sb.spell_name.empty() ? "?" : sb.spell_name.c_str();
                    std::string sbAmount = ui::format::formatAmount(sb.total_absorbed);
                    ImGui::Text("  %s: %s (%u)", spellName, sbAmount.c_str(), sb.event_count);
                }
            }
            ImGui::EndTooltip();
        }

        ImGui::SetCursorPos(ImVec2(cursorPos.x,
            cursorPos.y + config_.bar_height + config_.bar_spacing));
        ImGui::PopID();
    }

    ImGui::EndChild();

    ImGui::Separator();
    std::string totalStr = ui::format::formatAmount(cachedAbsorbTotal_);
    ImGui::Text("%s: %s", L("meter.total_absorbed"), totalStr.c_str());
}

// Shared implementation for Enemy Damage and Enemy Healing.
// Ranks hostile source actors from CombatDatabase and renders through the
// combat bar chart. Kept as a helper so both views share the throttling
// and time-window logic.
static void renderEnemyView(
    UIMeterPanel* self,
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    MeterViewType viewType,
    int32_t filterStartTime_ms,
    int32_t filterEndTime_ms,
    const MeterPanelConfig& config,
    uint32_t& lastRefreshTime_ms,
    uint32_t& lastPlaybackTime_ms,
    int32_t& cachedFilterStartTime_ms,
    int32_t& cachedFilterEndTime_ms,
    MeterViewType& cachedViewType,
    MeterTimeMode& cachedTimeMode,
    std::vector<ActorCombatStats>& cachedStats,
    int64_t& cachedGrandTotal,
    uint32_t& cachedDuration_ms,
    int instanceId,
    const char* emptyLabel,
    const char* chartLabel,
    const ImVec4& barColor
) {
    if (!combatDb || combatDb->empty()) {
        ImGui::Text("%s", emptyLabel);
        return;
    }

    uint32_t now_ms = static_cast<uint32_t>(ImGui::GetTime() * 1000.0f);
    bool timeScrubbed = (currentTime_ms > lastPlaybackTime_ms + 500) ||
                        (lastPlaybackTime_ms > currentTime_ms + 500);
    bool timeModeSwitched = (config.time_mode != cachedTimeMode);
    bool filterChanged = (filterStartTime_ms != cachedFilterStartTime_ms)
                      || (filterEndTime_ms != cachedFilterEndTime_ms);
    bool needsRefresh = cachedStats.empty()
        || cachedViewType != viewType
        || timeScrubbed
        || timeModeSwitched
        || filterChanged
        || (now_ms - lastRefreshTime_ms >= 1000);

    if (needsRefresh) {
        uint32_t dbMinTime = (filterStartTime_ms > 0)
            ? static_cast<uint32_t>(filterStartTime_ms)
            : combatDb->getMinTimestamp();
        uint32_t dbMaxTime = (filterEndTime_ms > 0)
            ? static_cast<uint32_t>(filterEndTime_ms)
            : combatDb->getMaxTimestamp();
        cachedFilterStartTime_ms = filterStartTime_ms;
        cachedFilterEndTime_ms = filterEndTime_ms;
        bool useFullEncounter = (config.time_mode == MeterTimeMode::FullEncounter);
        ui::TimeWindow window = ui::calculateTimeWindow(currentTime_ms, dbMinTime, dbMaxTime, useFullEncounter);

        cachedStats = (viewType == MeterViewType::EnemyHealing)
            ? combatDb->getRankedByEnemyHealing(window.startTime, window.endTime, 40)
            : combatDb->getRankedByEnemyDamage(window.startTime, window.endTime, 40);

        cachedGrandTotal = 0;
        for (const auto& s : cachedStats) {
            cachedGrandTotal += s.effective_amount;
        }
        cachedDuration_ms = window.duration_ms;
        cachedViewType = viewType;
        cachedTimeMode = config.time_mode;
        lastRefreshTime_ms = now_ms;
        lastPlaybackTime_ms = currentTime_ms;
    }

    if (cachedStats.empty()) {
        ImGui::Text("%s", emptyLabel);
        return;
    }

    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    float chartHeight = availableSize.y - 30.0f;
    if (chartHeight < 100.0f) chartHeight = 100.0f;

    std::string chartId = std::string(chartLabel) + "##" + std::to_string(instanceId);
    ImGui::BeginChild(chartId.c_str(), ImVec2(0, chartHeight), false, ImGuiWindowFlags_HorizontalScrollbar);

    float availableWidth = ImGui::GetContentRegionAvail().x;
    int64_t maxAmount = cachedStats.front().effective_amount;
    if (maxAmount <= 0) maxAmount = 1;
    float durationSeconds = cachedDuration_ms > 0
        ? static_cast<float>(cachedDuration_ms) / 1000.0f
        : 0.0f;

    for (size_t i = 0; i < cachedStats.size(); ++i) {
        const auto& stats = cachedStats[i];

        std::string actorName;
        std::string_view guidView(stats.actor_guid);
        auto nameIt = guidToName.find(guidView);
        if (nameIt != guidToName.end()) {
            actorName = std::string(nameIt->second);
        } else if (combatGuidToName) {
            auto combatNameIt = combatGuidToName->find(stats.actor_guid);
            if (combatNameIt != combatGuidToName->end()) {
                actorName = combatNameIt->second;
            }
        }
        if (actorName.empty()) {
            actorName = stats.actor_guid.substr(0, 12);
        }
        if (ui::utf8CharCount(actorName) > 16) {
            actorName = ui::utf8Truncate(actorName, 14) + "..";
        }

        ImVec4 color = barColor;
        if (colorGen) {
            ActorColor c = colorGen->getCachedColor(stats.actor_guid);
            color = ImVec4(c.r, c.g, c.b, 1.0f);
        }

        float ratio = static_cast<float>(stats.effective_amount) / static_cast<float>(maxAmount);
        float barWidth = (availableWidth - 180.0f) * ratio;
        if (barWidth < 5.0f) barWidth = 5.0f;

        ImGui::PushID(static_cast<int>(i * 10 + instanceId));

        ImVec2 cursorPos = ImGui::GetCursorPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetCursorScreenPos(),
            ImVec2(ImGui::GetCursorScreenPos().x + barWidth,
                   ImGui::GetCursorScreenPos().y + config.bar_height),
            ImGui::ColorConvertFloat4ToU32(color)
        );

        ImGui::SetCursorPos(ImVec2(cursorPos.x + 5.0f,
            cursorPos.y + (config.bar_height - ImGui::GetTextLineHeight()) / 2.0f));
        ImGui::Text("%s", actorName.c_str());

        std::string amountStr = ui::format::formatAmount(stats.effective_amount);
        float perSecond = durationSeconds > 0.0f
            ? static_cast<float>(stats.effective_amount) / durationSeconds
            : 0.0f;
        std::string apsStr = ui::format::formatPerSecond(perSecond);

        float percent = cachedGrandTotal > 0
            ? (static_cast<float>(stats.effective_amount) / static_cast<float>(cachedGrandTotal)) * 100.0f
            : 0.0f;

        ImGui::SetCursorPos(ImVec2(cursorPos.x + availableWidth - 170.0f,
            cursorPos.y + (config.bar_height - ImGui::GetTextLineHeight()) / 2.0f));
        ImGui::Text("%s (%s, %.1f%%)", amountStr.c_str(), apsStr.c_str(), percent);

        ImGui::SetCursorPos(cursorPos);
        (void)iconLoader;
        // Details-style click: seed this enemy as the DamageTakenBy
        // target and jump to that view. Only for EnemyDamage - no
        // taken-by-healing pivot exists yet, so EnemyHealing rows are
        // not clickable.
        bool rowClicked = ImGui::InvisibleButton("##bar",
            ImVec2(availableWidth, config.bar_height));
        if (rowClicked && self && viewType == MeterViewType::EnemyDamage) {
            self->setSelectedTarget(stats.actor_guid);
            self->getConfig().view_type = MeterViewType::DamageTakenBy;
        }
        if (ImGui::IsItemHovered() && viewType == MeterViewType::EnemyDamage) {
            ImGui::SetTooltip("Click to see who damaged %s", actorName.c_str());
        }

        ImGui::SetCursorPos(ImVec2(cursorPos.x,
            cursorPos.y + config.bar_height + config.bar_spacing));
        ImGui::PopID();
    }

    ImGui::EndChild();

    ImGui::Separator();
    std::string totalStr = ui::format::formatAmount(cachedGrandTotal);
    ImGui::Text("%s: %s", L("meter.total"), totalStr.c_str());
}

void UIMeterPanel::renderEnemyDamageView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    renderEnemyView(
        this, combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName,
        MeterViewType::EnemyDamage, filterStartTime_ms_, filterEndTime_ms_,
        config_, lastRefreshTime_ms_, lastPlaybackTime_ms_,
        cachedFilterStartTime_ms_, cachedFilterEndTime_ms_,
        cachedViewType_, cachedTimeMode_, cachedCombatStats_, cachedGrandTotal_, cachedDuration_ms_,
        instanceId_,
        L("status.no_enemy_damage_data"), "EnemyDamageChart",
        ImVec4(0.75f, 0.35f, 0.35f, 1.0f)
    );
}

void UIMeterPanel::renderEnemyHealingView(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    renderEnemyView(
        this, combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName,
        MeterViewType::EnemyHealing, filterStartTime_ms_, filterEndTime_ms_,
        config_, lastRefreshTime_ms_, lastPlaybackTime_ms_,
        cachedFilterStartTime_ms_, cachedFilterEndTime_ms_,
        cachedViewType_, cachedTimeMode_, cachedCombatStats_, cachedGrandTotal_, cachedDuration_ms_,
        instanceId_,
        L("status.no_enemy_healing_data"), "EnemyHealingChart",
        ImVec4(0.35f, 0.65f, 0.45f, 1.0f)
    );
}
