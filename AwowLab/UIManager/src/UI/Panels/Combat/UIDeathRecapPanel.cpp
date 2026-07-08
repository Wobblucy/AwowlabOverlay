#include "UI/Panels/Combat/UIDeathRecapPanel.h"
#include "UI/ISpellIconRenderer.h"
#include "UI/AwlUI/Widgets.h"
#include "Color/ActorColorGenerator.h"
#include "Core/NumberFormatter.h"
#include "Core/LocalizationManager.h"
#include "Core/SpellNameDatabase.h"
#include "imgui.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace {

// Same fallback pattern the meter panels use for spell names.
const char* resolveSpellName(
    uint32_t spellId,
    const std::unordered_map<uint32_t, std::string>* fallback) {
    if (fallback) {
        auto it = fallback->find(spellId);
        if (it != fallback->end() && !it->second.empty()) {
            return it->second.c_str();
        }
    }
    return SPELL_NAME(spellId);
}

}  // namespace

void UIDeathRecapPanel::open(const std::string& actorGuid,
                             const std::string& actorName,
                             std::vector<DeathEvent> deaths) {
    actorGuid_ = actorGuid;
    actorName_ = actorName;
    deaths_ = std::move(deaths);
    std::sort(deaths_.begin(), deaths_.end(),
              [](const DeathEvent& a, const DeathEvent& b) {
                  return a.timestamp_ms < b.timestamp_ms;
              });
    // Auto-select the most recent death so the right pane has content
    // on first open. If the actor has no deaths at all we still show
    // the modal with a "no deaths recorded" message.
    selectedDeathIndex_ = deaths_.empty() ? SIZE_MAX : deaths_.size() - 1;
    cachedEvents_.clear();
    cachedHealthHistory_.clear();
    cachedMaxHealth_ = 0;
    isOpen_ = true;
    shouldOpenPopup_ = true;
}

void UIDeathRecapPanel::close() {
    isOpen_ = false;
    actorGuid_.clear();
    actorName_.clear();
    deaths_.clear();
    cachedEvents_.clear();
    cachedHealthHistory_.clear();
    cachedMaxHealth_ = 0;
    selectedDeathIndex_ = SIZE_MAX;
}

void UIDeathRecapPanel::render(
    const CombatDatabase* combatDb,
    const ResourceDatabase* resourceDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    const std::unordered_map<uint32_t, std::string>* spellNameFallback,
    const AuraDatabase* auraDb,
    const std::vector<uint32_t>* trackedDefensiveIds
) {
    (void)colorGen;
    (void)guidToName;
    (void)combatGuidToName;

    if (!isOpen_) return;

    // Regular window rather than a modal so the timeline stays usable
    // while reviewing a death. Stay pinned to the viewport center
    // while the overlay auto-grows to fit us, then release for
    // free dragging.
    if (shouldOpenPopup_) {
        ImGui::SetNextWindowFocus();
        shouldOpenPopup_ = false;
        recenterFrames_ = 30;  // ~half a second at 60fps covers the auto-grow
        // Load the auto-selected death (most recent) so the right pane
        // has content the moment the window opens.
        loadSelectedDeath(combatDb, resourceDb, auraDb, trackedDefensiveIds);
    }
    if (recenterFrames_ > 0) {
        --recenterFrames_;
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }

    ImGui::SetNextWindowSize(ImVec2(1200, 750), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(L("death_recap.title"), &isOpen_, ImGuiWindowFlags_NoCollapse)) {
        // Header: actor name + total death count.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        ImGui::Text("%s", actorName_.empty() ? actorGuid_.c_str() : actorName_.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled(L("death_recap.count_fmt"), deaths_.size());
        ImGui::Separator();

        // Body: left = death list, right = selected-death analysis.
        float bodyHeight = ImGui::GetContentRegionAvail().y - 40.0f;
        if (bodyHeight < 200.0f) bodyHeight = 200.0f;
        constexpr float kListWidth = 260.0f;

        // Save the index the user was on before we render the list so
        // we can detect a change and reload cached data.
        size_t prevIndex = selectedDeathIndex_;

        ImGui::BeginChild("##deathlist", ImVec2(kListWidth, bodyHeight), true);
        renderDeathList();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##selected", ImVec2(0, bodyHeight), true);
        renderSelectedDeathPanel(iconLoader, spellNameFallback);
        ImGui::EndChild();

        if (selectedDeathIndex_ != prevIndex) {
            loadSelectedDeath(combatDb, resourceDb, auraDb, trackedDefensiveIds);
        }

        ImGui::Separator();
        if (awlui::Button(L("btn.close"), awlui::ButtonVariant::Primary,
                          awlui::ButtonSize::Md, ImVec2(120, 0))) {
            isOpen_ = false;
        }
        lastMeasuredSize_ = ImGui::GetWindowSize();
    }
    // Begin/End pairing: End runs even when the window is collapsed
    ImGui::End();

    if (!isOpen_) {
        close();
    }
}

void UIDeathRecapPanel::loadSelectedDeath(const CombatDatabase* combatDb,
                                          const ResourceDatabase* resourceDb,
                                          const AuraDatabase* auraDb,
                                          const std::vector<uint32_t>* trackedDefensiveIds) {
    cachedEvents_.clear();
    cachedHealthHistory_.clear();
    cachedDefensives_.clear();
    cachedMaxHealth_ = 0;
    if (selectedDeathIndex_ >= deaths_.size()) return;

    const DeathEvent& death = deaths_[selectedDeathIndex_];
    int32_t startTime = (death.timestamp_ms > 30000) ? death.timestamp_ms - 30000 : 0;

    // Defensive summary: for each tracked spell id, was it pressed in the
    // last 10s, still active at death, or absent? Driven off the aura
    // instances the AuraDatabase built for this player.
    if (auraDb && trackedDefensiveIds && !trackedDefensiveIds->empty()) {
        int32_t deathT = death.timestamp_ms;
        int32_t windowStart = deathT - kDefensiveWindowMs;
        auto instances = auraDb->getAllAurasOnTarget(death.actor_guid);
        for (uint32_t spellId : *trackedDefensiveIds) {
            // Find the best-matching instance for this spell id: prefer one
            // active at death, else the most recent applied within the window.
            const AuraInstance* activeAtDeath = nullptr;
            const AuraInstance* pressedInWindow = nullptr;
            std::string name;
            for (const auto* inst : instances) {
                if (inst->spell_id != spellId) continue;
                if (name.empty()) name = inst->spell_name;
                if (inst->isActiveAt(deathT)) {
                    activeAtDeath = inst;
                }
                if (inst->applied_at_ms >= windowStart && inst->applied_at_ms <= deathT) {
                    if (!pressedInWindow || inst->applied_at_ms > pressedInWindow->applied_at_ms) {
                        pressedInWindow = inst;
                    }
                }
            }

            DefensiveStatus status;
            status.spell_id = spellId;
            status.name = name;  // empty if we never saw this id for this player
            if (pressedInWindow) {
                // Pressed within the window (whether or not still up at death).
                status.state = DefensiveStatus::State::Pressed;
                status.seconds_before = (deathT - pressedInWindow->applied_at_ms) / 1000;
            } else if (activeAtDeath) {
                // Applied earlier but still covering them when they died.
                status.state = DefensiveStatus::State::ActiveAtDeath;
            } else {
                status.state = DefensiveStatus::State::Absent;
            }
            cachedDefensives_.push_back(std::move(status));
        }
    }

    if (combatDb) {
        // 0 = no cap on returned events; the UI filters via the slider.
        cachedEvents_ = combatDb->getEventsForTarget(
            death.actor_guid, startTime, death.timestamp_ms, 0);
    }

    if (resourceDb) {
        auto all = resourceDb->getAllResourcesForActor(death.actor_guid);
        for (const auto& e : all) {
            if (e.timestamp_ms >= startTime && e.timestamp_ms <= death.timestamp_ms) {
                cachedHealthHistory_.push_back(e);
                if (e.max_health > cachedMaxHealth_) {
                    cachedMaxHealth_ = e.max_health;
                }
            }
        }
    }
}

void UIDeathRecapPanel::renderDeathList() {
    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "Deaths");
    ImGui::Separator();

    if (deaths_.empty()) {
        ImGui::TextDisabled("No deaths recorded.");
        return;
    }

    for (size_t i = 0; i < deaths_.size(); ++i) {
        const auto& d = deaths_[i];
        std::string label = "#" + std::to_string(i + 1) + "  " + formatTimestamp(d.timestamp_ms);
        bool isSelected = (i == selectedDeathIndex_);
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
            selectedDeathIndex_ = i;
        }
        if (d.killing_spell_id != 0 && !d.killing_spell_name.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.55f, 1.0f));
            ImGui::Text("   %s", d.killing_spell_name.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
}

void UIDeathRecapPanel::renderSelectedDeathPanel(
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<uint32_t, std::string>* spellNameFallback) {

    if (selectedDeathIndex_ >= deaths_.size()) {
        ImGui::TextDisabled("Select a death to analyze.");
        return;
    }
    const DeathEvent& death = deaths_[selectedDeathIndex_];

    // Header: death timestamp + killing ability.
    ImGui::Text("Died at %s", formatTimestamp(death.timestamp_ms).c_str());
    if (death.killing_spell_id != 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("   |   Killing blow:");
        ImGui::SameLine();
        if (iconLoader && death.killing_spell_id > 0) {
            iconLoader->renderSpellIcon(death.killing_spell_id, ImVec2(16, 16));
            ImGui::SameLine();
        }
        const char* name = !death.killing_spell_name.empty()
            ? death.killing_spell_name.c_str()
            : resolveSpellName(death.killing_spell_id, spellNameFallback);
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", name);
    }

    // Defensives summary - "did they press / have a defensive before dying".
    renderDefensivesSummary();

    // Filter slider.
    ImGui::Spacing();
    ImGui::SetNextItemWidth(180.0f);
    awlui::SliderFloat("Hide hits under", &eventFilterThreshold_,
                       0.0f, 20.0f, "%.0f%% max HP");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Filters the event list below. Small ticks that\n"
                          "don't matter for post-mortem analysis are hidden\n"
                          "when their damage is under this percentage of\n"
                          "the player's max HP.");
    }

    ImGui::Separator();

    // HP chart (fixed height so the event list gets the rest).
    ImGui::Text("HP timeline (30s window)");
    renderHealthChart(ImGui::GetContentRegionAvail().x, 140.0f);

    ImGui::Spacing();
    ImGui::Separator();

    renderEventList(iconLoader, spellNameFallback);
}

void UIDeathRecapPanel::renderDefensivesSummary() {
    // Nothing to say if the user tracks no defensives (list empty ->
    // cachedDefensives_ empty). Keeps the panel unchanged for anyone who
    // hasn't configured the feature.
    if (cachedDefensives_.empty()) return;

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", L("defensives.title"));

    for (const auto& d : cachedDefensives_) {
        // Name: from the log if we ever saw the aura, else the raw id so
        // an absent tracked defensive still reads clearly.
        char nameBuf[64];
        const char* name = d.name.c_str();
        if (d.name.empty()) {
            snprintf(nameBuf, sizeof(nameBuf), "#%u", d.spell_id);
            name = nameBuf;
        }

        switch (d.state) {
            case DefensiveStatus::State::Pressed: {
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "+ %s", name);
                ImGui::SameLine();
                ImGui::TextDisabled(L("defensives.pressed_fmt"), d.seconds_before);
                break;
            }
            case DefensiveStatus::State::ActiveAtDeath: {
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "+ %s", name);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", L("defensives.active_at_death"));
                break;
            }
            case DefensiveStatus::State::Absent: {
                ImGui::TextDisabled("- %s   %s", name, L("defensives.none"));
                break;
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
}

void UIDeathRecapPanel::renderHealthChart(float width, float height) {
    if (selectedDeathIndex_ >= deaths_.size()) {
        ImGui::Dummy(ImVec2(width, height));
        return;
    }
    if (cachedHealthHistory_.empty()) {
        ImGui::TextDisabled("No HP data recorded in this window.");
        ImGui::Dummy(ImVec2(width, height));
        return;
    }

    const DeathEvent& death = deaths_[selectedDeathIndex_];
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    // Background + border.
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                        IM_COL32(30, 30, 40, 255));
    draw->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
                  IM_COL32(60, 60, 80, 255));

    // Horizontal grid lines every quarter.
    for (int i = 1; i < 4; ++i) {
        float y = pos.y + (height * i / 4.0f);
        draw->AddLine(ImVec2(pos.x, y),
                      ImVec2(pos.x + width, y),
                      IM_COL32(50, 50, 70, 128));
    }

    // Time window: 30s before death.
    int32_t deathTime = death.timestamp_ms;
    int32_t startTime = (deathTime > 30000) ? deathTime - 30000 : 0;
    float timeRange = static_cast<float>(deathTime - startTime);
    if (timeRange < 1.0f) timeRange = 1.0f;

    ImVec2 prev;
    bool first = true;
    for (const auto& e : cachedHealthHistory_) {
        if (e.timestamp_ms < startTime || e.timestamp_ms > deathTime) continue;
        float hpPct = (e.max_health > 0)
            ? static_cast<float>(e.current_health) / static_cast<float>(e.max_health)
            : 0.0f;
        hpPct = std::clamp(hpPct, 0.0f, 1.0f);
        float x = pos.x + ((e.timestamp_ms - startTime) / timeRange) * width;
        float y = pos.y + height - (hpPct * height);
        ImVec2 point(x, y);
        if (!first) {
            uint8_t r = static_cast<uint8_t>((1.0f - hpPct) * 255);
            uint8_t g = static_cast<uint8_t>(hpPct * 200);
            draw->AddLine(prev, point, IM_COL32(r, g, 80, 255), 2.0f);
        }
        prev = point;
        first = false;
    }

    // Final drop to 0% at the right edge.
    if (!first) {
        ImVec2 deathPt(pos.x + width, pos.y + height);
        draw->AddLine(prev, deathPt, IM_COL32(255, 0, 80, 255), 2.0f);
    }

    ImGui::Dummy(ImVec2(width, height));
    ImGui::TextDisabled("-30s                                              Death");
}

void UIDeathRecapPanel::renderEventList(
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<uint32_t, std::string>* spellNameFallback) {

    bool filterActive = cachedMaxHealth_ > 0 && eventFilterThreshold_ > 0.0f;
    size_t visibleCount = cachedEvents_.size();
    if (filterActive) {
        visibleCount = 0;
        for (const auto& e : cachedEvents_) {
            float pct = (static_cast<float>(e.amount) / static_cast<float>(cachedMaxHealth_)) * 100.0f;
            if (pct >= eventFilterThreshold_) ++visibleCount;
        }
        ImGui::Text("Events  (%zu of %zu shown)", visibleCount, cachedEvents_.size());
    } else {
        ImGui::Text("Events  (%zu)", cachedEvents_.size());
    }

    if (cachedEvents_.empty()) {
        ImGui::TextDisabled("No damage/healing events in the last 30 seconds.");
        return;
    }

    // Table fills whatever height is left in the right pane.
    float availHeight = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginTable("EventsTable", 3,
        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
        ImVec2(0, availHeight))) {

        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Ability", ImGuiTableColumnFlags_WidthStretch);
        // Wide enough for "-999.9K  (100% HP)" without clipping.
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableHeadersRow();

        const DeathEvent& death = deaths_[selectedDeathIndex_];

        for (size_t rowIdx = 0; rowIdx < cachedEvents_.size(); ++rowIdx) {
            const auto& e = cachedEvents_[rowIdx];
            if (filterActive) {
                float pct = (static_cast<float>(e.amount) / static_cast<float>(cachedMaxHealth_)) * 100.0f;
                if (pct < eventFilterThreshold_) continue;
            }

            // Find HP % at this event's timestamp (step-wise interpolation).
            float hpPct = 1.0f;
            for (auto it = cachedHealthHistory_.rbegin();
                 it != cachedHealthHistory_.rend(); ++it) {
                if (it->timestamp_ms <= e.timestamp_ms && it->max_health > 0) {
                    hpPct = static_cast<float>(it->current_health) / static_cast<float>(it->max_health);
                    hpPct = std::clamp(hpPct, 0.0f, 1.0f);
                    break;
                }
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImVec2 rowStartPos = ImGui::GetCursorScreenPos();

            // Draw the HP bar FIRST so the text renders on top of it.
            // Drawing after the text left the bar covering the text
            // slightly. The bar spans the whole table width filled to
            // HP% - visualizes at a glance when the player was in
            // trouble as you scan down the list.
            float rowHeight = ImGui::GetTextLineHeightWithSpacing();
            ImVec2 windowPos = ImGui::GetWindowPos();
            float windowWidth = ImGui::GetWindowContentRegionMax().x;
            float rowWidth = windowWidth - (rowStartPos.x - windowPos.x) + 8.0f;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImU32 barColor = e.is_damage
                ? IM_COL32(160, 50, 50, 180)
                : IM_COL32(50, 140, 60, 170);
            draw->AddRectFilled(
                rowStartPos,
                ImVec2(rowStartPos.x + rowWidth * hpPct, rowStartPos.y + rowHeight),
                barColor);

            // Time relative to death (negative = seconds before).
            int32_t offset = e.timestamp_ms - death.timestamp_ms;
            ImGui::TextUnformatted(formatRelativeTime(offset).c_str());

            // Ability with icon + real name.
            ImGui::TableSetColumnIndex(1);
            if (iconLoader && e.spell_id > 1) {
                if (iconLoader->renderSpellIcon(e.spell_id, ImVec2(16, 16))) {
                    ImGui::SameLine();
                }
            }
            if (e.spell_id == 0 || e.spell_id == 1) {
                ImGui::TextUnformatted(L("meter.melee"));
            } else {
                ImGui::TextUnformatted(resolveSpellName(e.spell_id, spellNameFallback));
            }
            if (e.is_crit) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "*");
            }

            // Amount + HP% at the time of the hit.
            ImGui::TableSetColumnIndex(2);
            std::string amountStr = ui::format::formatAmount(e.amount);
            if (e.is_damage) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.55f, 1.0f),
                                   "-%s  (%.0f%% HP)", amountStr.c_str(), hpPct * 100.0f);
            } else {
                ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.55f, 1.0f),
                                   "+%s  (%.0f%% HP)", amountStr.c_str(), hpPct * 100.0f);
            }
        }

        ImGui::EndTable();
    }
}

std::string UIDeathRecapPanel::formatTimestamp(int32_t timestamp_ms) {
    bool negative = timestamp_ms < 0;
    uint32_t absTime = negative ? static_cast<uint32_t>(-timestamp_ms) : static_cast<uint32_t>(timestamp_ms);

    uint32_t minutes = absTime / 60000;
    uint32_t seconds = (absTime % 60000) / 1000;
    uint32_t millis = absTime % 1000;

    std::ostringstream ss;
    if (negative) ss << "-";
    ss << minutes << ":" << std::setw(2) << std::setfill('0') << seconds
       << "." << std::setw(3) << std::setfill('0') << millis;
    return ss.str();
}

std::string UIDeathRecapPanel::formatRelativeTime(int32_t offset_ms) {
    // Offsets are relative to the death itself, so nearly all values
    // are negative. Format as "-3.2s" or similar.
    bool negative = offset_ms < 0;
    uint32_t abs_ms = negative ? static_cast<uint32_t>(-offset_ms)
                                : static_cast<uint32_t>(offset_ms);
    uint32_t sec = abs_ms / 1000;
    uint32_t hundredths = (abs_ms % 1000) / 10;
    std::ostringstream ss;
    if (negative) ss << "-";
    ss << sec << "." << std::setw(2) << std::setfill('0') << hundredths << "s";
    return ss.str();
}
