#include "UI/Panels/Combat/UIActorBreakdownPanel.h"
#include "UI/ISpellIconRenderer.h"
#include "Color/ActorColorGenerator.h"
#include "Core/NumberFormatter.h"
#include "Core/UIUtils.h"
#include "Core/LocalizationManager.h"
#include "Core/SpellNameDatabase.h"
#include <imgui.h>
#include <algorithm>

void UIActorBreakdownPanel::open(const std::string& actorGuid, CombatMetricType metricType) {
    actorGuid_ = actorGuid;
    metricType_ = metricType;
    actorStats_ = {};
    duration_ms_ = 0;
    selectedSpellIndex_ = -1;
    needsImmediateRefresh_ = true;  // Force refresh on first render
    isOpen_ = true;
    shouldOpenPopup_ = true;

    // Reset components
    graphRenderer_.reset();
    tableRenderer_.reset();
    tableRenderer_.loadSettings();
}

void UIActorBreakdownPanel::close() {
    isOpen_ = false;
    shouldOpenPopup_ = false;
}

void UIActorBreakdownPanel::refreshStats(const CombatDatabase* combatDb,
                                          uint32_t currentTime_ms,
                                          [[maybe_unused]] bool useFullEncounter) {
    if (!combatDb || actorGuid_.empty()) return;

    currentTime_ms_ = currentTime_ms;

    // Get database time bounds
    uint32_t dbMinTime = combatDb->getMinTimestamp();
    uint32_t dbMaxTime = combatDb->getMaxTimestamp();

    // Determine time window for stats (from graph selection or full encounter)
    uint32_t startTime, endTime;
    if (graphRenderer_.hasTimeSelection()) {
        startTime = graphRenderer_.getSelectionStartTime();
        endTime = graphRenderer_.getSelectionEndTime();
    } else {
        startTime = dbMinTime;
        endTime = dbMaxTime;
    }

    uint32_t windowDuration = (endTime > startTime) ? (endTime - startTime) : 1;

    // Get fresh stats for this actor (includeBreakdowns=true for full spell/target data)
    std::vector<ActorCombatStats> allStats;
    if (metricType_ == CombatMetricType::DamageTaken) {
        allStats = combatDb->getRankedByDamageTakenWithPets(startTime, endTime, 100, true);
    } else {
        allStats = combatDb->getRankedByActorWithPets(metricType_, startTime, endTime, 100, true);
    }

    // Find our actor in the results
    bool found = false;
    for (const auto& stats : allStats) {
        if (stats.actor_guid == actorGuid_) {
            actorStats_ = stats;
            duration_ms_ = windowDuration;
            found = true;
            break;
        }
    }

    if (!found) {
        actorStats_ = {};
        actorStats_.actor_guid = actorGuid_;
        duration_ms_ = windowDuration;
    }

    // FriendlyFire and HealingReceived are "synthetic" metrics without
    // a per-actor combat table backing them, so the ranking query above
    // returned this actor's raw damage/healing totals. Replace the
    // spell_breakdown with the target-filtered breakdown and recompute
    // the totals so the header reads correctly for the drill-down.
    if (metricType_ == CombatMetricType::FriendlyFire ||
        metricType_ == CombatMetricType::HealingReceived) {
        auto filtered = combatDb->getSpellBreakdown(actorGuid_, metricType_, startTime, endTime);
        actorStats_.spell_breakdown = std::move(filtered);
        int64_t total = 0;
        int64_t effective = 0;
        uint32_t hits = 0;
        uint32_t crits = 0;
        for (const auto& s : actorStats_.spell_breakdown) {
            total += s.total_amount;
            effective += s.effective_amount;
            hits += s.hit_count;
            crits += s.crit_count;
        }
        actorStats_.total_amount = total;
        actorStats_.effective_amount = effective;
        actorStats_.hit_count = hits;
        actorStats_.crit_count = crits;
        float durationSeconds = windowDuration > 0
            ? static_cast<float>(windowDuration) / 1000.0f
            : 1.0f;
        actorStats_.amount_per_second = static_cast<float>(effective) / durationSeconds;
    }

    // Update table renderer with new stats
    tableRenderer_.setActorStats(&actorStats_, metricType_);
    tableRenderer_.buildGroupedSpellRows();

    // Fetch time series data for graph (always full encounter)
    // Pass blacklist to filter out blacklisted spells from the graph
    const auto& blacklist = tableRenderer_.getGroupSettings().getBlacklistedSpells();
    const std::unordered_set<uint32_t>* blacklistPtr = blacklist.empty() ? nullptr : &blacklist;

    std::vector<DamageTimeBucket> timeSeries;
    if (metricType_ == CombatMetricType::HealingDone) {
        timeSeries = combatDb->getActorHealingTimeSeries(actorGuid_, 1000, blacklistPtr);
    } else if (metricType_ == CombatMetricType::DamageTaken) {
        timeSeries = combatDb->getActorDamageTakenTimeSeries(actorGuid_, 1000, blacklistPtr);
    } else {
        timeSeries = combatDb->getActorDamageTimeSeries(actorGuid_, 1000, blacklistPtr);
    }

    // Update graph renderer with new time series
    graphRenderer_.updateTimeSeries(timeSeries, tableRenderer_.getTopSpellIds(), dbMinTime, dbMaxTime);
}

void UIActorBreakdownPanel::render(
    const CombatDatabase* combatDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    awow::ISpellIconRenderer* iconLoader,
    uint32_t currentTime_ms,
    bool useFullEncounter,
    const std::unordered_map<uint32_t, std::string>* spellNameFallback
) {
    if (!isOpen_) {
        return;
    }

    // Stash for renderSpellSidebar and forward to the table renderer
    // so overlay drill-downs resolve spell names from the combat log
    // rather than the (unloaded) SpellDataTable.
    spellNameFallback_ = spellNameFallback;
    tableRenderer_.setSpellNameFallback(spellNameFallback);

    // Get spec ID from color generator for spell grouping
    if (colorGen) {
        tableRenderer_.setSpecId(colorGen->getSpecId(actorGuid_));
    }

    // Check if we need to refresh stats (every second, or immediately on first render)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRefresh_).count();
    if (needsImmediateRefresh_ || elapsed >= REFRESH_INTERVAL_MS) {
        refreshStats(combatDb, currentTime_ms, useFullEncounter);
        lastRefresh_ = now;
        needsImmediateRefresh_ = false;
    }

    // Regular window rather than a modal: the rest of the app stays
    // interactive (scrub the timeline, click other actors) while the
    // breakdown is open. Stay pinned to the viewport center while the
    // overlay auto-grows to fit us, then release so the user can
    // move/resize freely.
    //
    // The initial size needs to accommodate: chart (full width) +
    // (spell table with ~700px minimum for fixed columns and the
    // stretchy Spell name column) + (280px sidebar) + child padding.
    // Undershooting the initial guess clips the sidebar on the first
    // frame before the auto-grow catches up on frame 2.
    if (shouldOpenPopup_) {
        ImGui::SetNextWindowFocus();
        shouldOpenPopup_ = false;
        recenterFrames_ = 30;  // ~half a second at 60fps covers the auto-grow
    }
    if (recenterFrames_ > 0) {
        --recenterFrames_;
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }
    ImGui::SetNextWindowSize(ImVec2(1500, 950), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(L("breakdown.title"), &isOpen_, ImGuiWindowFlags_NoCollapse)) {

        // Update current time for playback indicator (realtime, not just every second)
        currentTime_ms_ = currentTime_ms;

        // Check if viewing the Blacklist virtual actor
        bool isBlacklistActor = (actorGuid_ == BLACKLIST_ACTOR_GUID);

        // Header section
        renderHeader(guidToName, colorGen);

        // Damage graph with playback indicator (skip for Blacklist actor - no time series data)
        if (!isBlacklistActor) {
            bool graphSelectionChanged = graphRenderer_.render(metricType_, currentTime_ms_);
            if (graphSelectionChanged) {
                needsImmediateRefresh_ = true;
            }
        }

        ImGui::Separator();

        // Main content: spell table on left, sidebar on right
        float sidebarWidth = 280.0f;
        float tableWidth = ImGui::GetContentRegionAvail().x - sidebarWidth - 10.0f;

        // Left side: Spell table and targets
        ImGui::BeginChild("LeftPane", ImVec2(tableWidth, 0), false);
        {
            // Spell table
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", L("breakdown.spells"));
            ImGui::Separator();

            bool needsRefresh = false;
            tableRenderer_.render(iconLoader, actorGuid_, selectedSpellIndex_, needsRefresh);
            if (needsRefresh) {
                needsImmediateRefresh_ = true;
            }

            // Skip targets/pets sections for the Blacklist actor
            if (!isBlacklistActor) {
                ImGui::Spacing();
                ImGui::Spacing();

                // Targets section
                renderTargetsSection(guidToName);

                // Pets section
                renderPetsSection(guidToName);
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right side: Spell detail sidebar
        ImGui::BeginChild("RightPane", ImVec2(sidebarWidth, 0), true);
        {
            renderSpellSidebar(iconLoader);
        }
        ImGui::EndChild();

        // Record the size ImGui actually laid out so the outer overlay
        // window can auto-grow to fit next frame. Includes ImGui's
        // internal padding, borders and title bar.
        lastMeasuredSize_ = ImGui::GetWindowSize();
    }
    // Begin/End pairing: End runs even when the window is collapsed
    ImGui::End();

    // Tidy up open-state flags when the title-bar X closed us
    if (!isOpen_) {
        close();
    }
}

void UIActorBreakdownPanel::renderHeader(
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    const std::shared_ptr<ActorColorGenerator>& colorGen
) {
    bool isBlacklistActor = (actorGuid_ == BLACKLIST_ACTOR_GUID);
    bool isHealing = (metricType_ == CombatMetricType::HealingDone);
    bool isDamageTaken = (metricType_ == CombatMetricType::DamageTaken);

    // Get actor name and color (special handling for Blacklist actor)
    std::string actorName;
    ImVec4 actorColor(1.0f, 1.0f, 1.0f, 1.0f);

    if (isBlacklistActor) {
        actorName = L("breakdown.blacklisted_spells_title");
        actorColor = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);  // Gray for blacklist
    } else {
        actorName = ui::resolveActorName(actorGuid_, guidToName);
        if (colorGen) {
            auto color = colorGen->getCachedColor(actorGuid_);
            actorColor = ImVec4(color.r, color.g, color.b, 1.0f);
        }
    }

    // Actor name in their class color
    ImGui::TextColored(actorColor, "%s", actorName.c_str());
    if (!isBlacklistActor) {
        ImGui::SameLine();
        const char* metricLabel;
        if (isHealing) {
            metricLabel = L("breakdown.healing_done");
        } else if (isDamageTaken) {
            metricLabel = L("breakdown.damage_taken");
        } else {
            metricLabel = L("breakdown.damage_done");
        }
        ImGui::TextDisabled("- %s", metricLabel);
    }

    // Stats line
    std::string totalStr = ui::format::formatAmount(actorStats_.total_amount);
    float perSecond = (duration_ms_ > 0)
        ? static_cast<float>(actorStats_.effective_amount) / (static_cast<float>(duration_ms_) / 1000.0f)
        : 0.0f;
    std::string perSecondStr = ui::format::formatPerSecond(perSecond);

    float critPercent = (actorStats_.hit_count > 0)
        ? static_cast<float>(actorStats_.crit_count) / static_cast<float>(actorStats_.hit_count) * 100.0f
        : 0.0f;

    const char* perSecondLabel = isHealing ? "HPS" : (isDamageTaken ? "DTPS" : "DPS");
    ImGui::Text("Total: %s  |  %s: %s  |  Hits: %u  |  Crit: %.1f%%",
                totalStr.c_str(), perSecondLabel, perSecondStr.c_str(), actorStats_.hit_count, critPercent);
}

void UIActorBreakdownPanel::renderSpellSidebar(awow::ISpellIconRenderer* iconLoader) {
    if (selectedSpellIndex_ < 0 ||
        selectedSpellIndex_ >= static_cast<int>(actorStats_.spell_breakdown.size())) {
        ImGui::TextDisabled("%s", L("breakdown.click_to_see"));
        return;
    }

    const auto& spell = actorStats_.spell_breakdown[selectedSpellIndex_];
    const float iconSize = 32.0f;

    // Spell header with icon
    if (iconLoader) {
        if (iconLoader->renderSpellIcon(spell.spell_id, ImVec2(iconSize, iconSize))) {
            ImGui::SameLine();
        }
    }

    if (spell.spell_id == 1) {
        ImGui::Text("%s", L("meter.melee"));
    } else {
        const char* name = nullptr;
        if (spellNameFallback_) {
            auto it = spellNameFallback_->find(spell.spell_id);
            if (it != spellNameFallback_->end() && !it->second.empty()) {
                name = it->second.c_str();
            }
        }
        if (!name) name = SPELL_NAME(spell.spell_id);
        ImGui::Text("%s", name);
    }

    ImGui::Separator();

    // Overall stats
    bool isHealing = (metricType_ == CombatMetricType::HealingDone);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Hits: %u", spell.hit_count);
    ImGui::Text("%s: %s", isHealing ? "Healing" : "Damage", ui::format::formatAmount(spell.total_amount).c_str());

    // Average per hit
    int64_t avgHit = (spell.hit_count > 0) ? spell.total_amount / spell.hit_count : 0;
    ImGui::Text("Average: %s", ui::format::formatAmount(avgHit).c_str());

    // Per-second for this spell
    float spellPerSecond = (duration_ms_ > 0)
        ? static_cast<float>(spell.effective_amount) / (static_cast<float>(duration_ms_) / 1000.0f)
        : 0.0f;
    ImGui::Text("%s: %s", isHealing ? "HPS" : "DPS", ui::format::formatPerSecond(spellPerSecond).c_str());

    ImGui::Spacing();
    ImGui::Separator();

    // Normal hits section
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", L("breakdown.normal_hits"));

    float normalPercent = (spell.hit_count > 0)
        ? static_cast<float>(spell.normal_count) / static_cast<float>(spell.hit_count) * 100.0f
        : 0.0f;
    ImGui::Text("%u (%.1f%%)", spell.normal_count, normalPercent);

    if (spell.normal_count > 0) {
        int64_t normalAvg = spell.normal_total / spell.normal_count;
        ImGui::Text(L("breakdown.min"), ui::format::formatAmount(
            spell.normal_min == INT64_MAX ? 0 : spell.normal_min).c_str());
        ImGui::Text(L("breakdown.max"), ui::format::formatAmount(spell.normal_max).c_str());
        ImGui::Text(L("breakdown.average"), ui::format::formatAmount(normalAvg).c_str());

        // Normal per-second
        float normalPerSecond = (duration_ms_ > 0)
            ? static_cast<float>(spell.normal_total) / (static_cast<float>(duration_ms_) / 1000.0f)
            : 0.0f;
        ImGui::Text("%s: %s", isHealing ? "HPS" : "DPS", ui::format::formatPerSecond(normalPerSecond).c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Critical hits section
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", L("breakdown.critical_hits"));

    float critPercent = (spell.hit_count > 0)
        ? static_cast<float>(spell.crit_count) / static_cast<float>(spell.hit_count) * 100.0f
        : 0.0f;
    ImGui::Text("%u (%.1f%%)", spell.crit_count, critPercent);

    if (spell.crit_count > 0) {
        int64_t critAvg = spell.crit_total / spell.crit_count;
        ImGui::Text(L("breakdown.min"), ui::format::formatAmount(
            spell.crit_min == INT64_MAX ? 0 : spell.crit_min).c_str());
        ImGui::Text(L("breakdown.max"), ui::format::formatAmount(spell.crit_max).c_str());
        ImGui::Text(L("breakdown.average"), ui::format::formatAmount(critAvg).c_str());

        // Crit per-second
        float critPerSecond = (duration_ms_ > 0)
            ? static_cast<float>(spell.crit_total) / (static_cast<float>(duration_ms_) / 1000.0f)
            : 0.0f;
        ImGui::Text("%s: %s", isHealing ? "HPS" : "DPS", ui::format::formatPerSecond(critPerSecond).c_str());
    }
}

void UIActorBreakdownPanel::renderTargetsSection(
    const std::unordered_map<std::string_view, std::string_view>& guidToName
) {
    if (actorStats_.target_breakdown.empty()) {
        return;
    }

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", L("breakdown.targets"));
    ImGui::Separator();

    // Show top 10 targets
    size_t maxTargets = std::min(actorStats_.target_breakdown.size(), size_t(10));

    for (size_t i = 0; i < maxTargets; ++i) {
        const auto& target = actorStats_.target_breakdown[i];
        std::string targetName = ui::resolveActorName(target.target_guid, guidToName, 25);

        float percent = (actorStats_.total_amount > 0)
            ? static_cast<float>(target.total_amount) / static_cast<float>(actorStats_.total_amount) * 100.0f
            : 0.0f;

        ImGui::Text("%zu. %s: %s (%.1f%%)",
                    i + 1,
                    targetName.c_str(),
                    ui::format::formatAmount(target.total_amount).c_str(),
                    percent);
    }

    if (actorStats_.target_breakdown.size() > 10) {
        ImGui::TextDisabled(L("breakdown.more_targets"),
                           actorStats_.target_breakdown.size() - 10);
    }

    ImGui::Spacing();
}

void UIActorBreakdownPanel::renderPetsSection(
    const std::unordered_map<std::string_view, std::string_view>& guidToName
) {
    if (actorStats_.pet_breakdown.empty()) {
        return;
    }

    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.8f, 1.0f), "%s", L("breakdown.pets_guardians"));
    ImGui::Separator();

    for (const auto& pet : actorStats_.pet_breakdown) {
        std::string petName = ui::resolveActorName(pet.pet_guid, guidToName, 25);

        float percent = (actorStats_.total_amount > 0)
            ? static_cast<float>(pet.total_amount) / static_cast<float>(actorStats_.total_amount) * 100.0f
            : 0.0f;

        ImGui::Text("%s: %s (%.1f%%)",
                    petName.c_str(),
                    ui::format::formatAmount(pet.total_amount).c_str(),
                    percent);
    }

    ImGui::Spacing();
}
