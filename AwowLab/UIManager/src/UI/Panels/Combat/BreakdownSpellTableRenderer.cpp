#include "UI/Panels/Combat/BreakdownSpellTableRenderer.h"
#include "UI/ISpellIconRenderer.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/SpellContextMenu.h"
#include "Core/NumberFormatter.h"
#include "Core/LocalizationManager.h"
#include "Core/PhaseSettings.h"
#include "Core/SpellNameDatabase.h"
#include <algorithm>
#include <cstring>

void BreakdownSpellTableRenderer::reset() {
    actorStats_ = nullptr;
    groupedSpellRows_.clear();
    blacklistedSpellRows_.clear();
    collapsedPetGroups_.clear();
    topSpellIds_.clear();
    needsGroupRebuild_ = false;
    isEditingGroupName_ = false;
    editingGroupId_ = 0;
}

void BreakdownSpellTableRenderer::setActorStats(const ActorCombatStats* stats, CombatMetricType metricType) {
    actorStats_ = stats;
    metricType_ = metricType;

    // Build list of top spells for consistent coloring
    topSpellIds_.clear();
    if (stats && !stats->spell_breakdown.empty()) {
        size_t numSpells = std::min(stats->spell_breakdown.size(), size_t(15));
        for (size_t i = 0; i < numSpells; ++i) {
            topSpellIds_.push_back(stats->spell_breakdown[i].spell_id);
        }
    }
}

void BreakdownSpellTableRenderer::buildGroupedSpellRows() {
    groupedSpellRows_.clear();
    blacklistedSpellRows_.clear();

    if (!actorStats_) return;

    const auto* specGroups = spellGroupSettings_.getSpecGroups(currentSpecId_);
    std::unordered_set<uint32_t> groupedSpellIds;
    const auto& blacklist = spellGroupSettings_.getBlacklistedSpells();

    // First, add all groups and their spells (skip blacklisted)
    if (specGroups) {
        for (const auto& group : specGroups->groups) {
            // Add group header
            GroupedSpellRow headerRow;
            headerRow.type = GroupedSpellRow::Type::GroupHeader;
            headerRow.group_id = group.group_id;
            headerRow.spell = nullptr;
            headerRow.group_total = 0;
            headerRow.group_name = group.name;

            // Find spells belonging to this group and calculate total (skip blacklisted)
            std::vector<const SpellCombatStats*> groupSpells;
            for (uint32_t spell_id : group.spell_ids) {
                // Skip blacklisted spells
                if (blacklist.find(spell_id) != blacklist.end()) {
                    continue;
                }
                for (const auto& spell : actorStats_->spell_breakdown) {
                    if (spell.spell_id == spell_id) {
                        groupSpells.push_back(&spell);
                        headerRow.group_total += spell.total_amount;
                        groupedSpellIds.insert(spell_id);
                        break;
                    }
                }
            }

            // Only add group if it has spells in the current breakdown
            if (!groupSpells.empty() || group.spell_ids.empty()) {
                groupedSpellRows_.push_back(headerRow);

                // Add spells under this group (if not collapsed)
                if (!group.collapsed) {
                    for (const auto* spell : groupSpells) {
                        GroupedSpellRow spellRow;
                        spellRow.type = GroupedSpellRow::Type::Spell;
                        spellRow.group_id = group.group_id;
                        spellRow.spell = spell;
                        spellRow.group_total = 0;
                        groupedSpellRows_.push_back(spellRow);
                    }
                }
            }
        }
    }

    // Then add ungrouped spells (non-blacklisted)
    for (const auto& spell : actorStats_->spell_breakdown) {
        // Check if blacklisted
        if (blacklist.find(spell.spell_id) != blacklist.end()) {
            blacklistedSpellRows_.push_back(&spell);
            continue;
        }
        // Check if already in a group
        if (groupedSpellIds.find(spell.spell_id) == groupedSpellIds.end()) {
            GroupedSpellRow row;
            row.type = GroupedSpellRow::Type::Spell;
            row.group_id = 0;  // Ungrouped
            row.spell = &spell;
            row.group_total = 0;
            groupedSpellRows_.push_back(row);
        }
    }

    // Finally, auto-generated per-pet-type groups after the owner's own
    // spells (the player's own damage stays primary). Each pet TYPE is one
    // collapsible header with its abilities beneath. Not user-editable, so
    // they aren't checked against blacklist/user groups.
    for (const auto& petGroup : actorStats_->pet_spell_groups) {
        GroupedSpellRow headerRow;
        headerRow.type = GroupedSpellRow::Type::PetGroupHeader;
        headerRow.group_id = 0;
        headerRow.spell = nullptr;
        headerRow.group_total = petGroup.total_amount;
        headerRow.pet_npc_id = petGroup.npc_id;
        headerRow.pet_guid = petGroup.pet_guid;
        headerRow.group_name = petGroup.pet_guid;  // resolved to a display name at render time
        groupedSpellRows_.push_back(headerRow);

        bool collapsed = collapsedPetGroups_.find(
            petGroupKey(petGroup.npc_id, petGroup.pet_guid)) != collapsedPetGroups_.end();
        if (!collapsed) {
            for (const auto& spell : petGroup.spells) {
                GroupedSpellRow spellRow;
                spellRow.type = GroupedSpellRow::Type::Spell;
                spellRow.group_id = 1;  // non-zero so the row indents like a grouped spell
                spellRow.spell = &spell;
                spellRow.group_total = 0;
                groupedSpellRows_.push_back(spellRow);
            }
        }
    }
}

void BreakdownSpellTableRenderer::render(awow::ISpellIconRenderer* iconLoader, const std::string& actorGuid,
                                          int& selectedSpellIndex, bool& needsRefresh) {
    if (!actorStats_) return;

    // Check if we need to rebuild from previous frame (deferred to avoid iterator invalidation)
    if (needsGroupRebuild_) {
        buildGroupedSpellRows();
        needsGroupRebuild_ = false;
    }

    bool isBlacklistActor = (actorGuid == BLACKLIST_ACTOR_GUID);

    // Spell grouping controls (hide for Blacklist actor)
    if (!isBlacklistActor) {
        renderGroupingControls(needsRefresh);
    }

    // Use modern Table API instead of legacy Columns
    ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable;

    float tableHeight = ImGui::GetContentRegionAvail().y - 150.0f;  // Leave room for targets/pets
    if (tableHeight < 150.0f) tableHeight = 150.0f;

    bool isHealing = (metricType_ == CombatMetricType::HealingDone);
    if (ImGui::BeginTable("SpellTable", 9, tableFlags, ImVec2(0, tableHeight))) {
        // Setup columns
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Spell", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Raw", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Effective", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableSetupColumn(isHealing ? "Over%" : "Mitig%", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Crit%", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Hits", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupScrollFreeze(0, 1);  // Freeze header row
        ImGui::TableHeadersRow();

        // Add tooltip for "Raw" column header
        if (ImGui::TableGetColumnFlags(3) & ImGuiTableColumnFlags_IsHovered) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", L("breakdown.raw_tooltip"));
            ImGui::TextDisabled("%s", L("breakdown.raw_tooltip_crit1"));
            ImGui::TextDisabled("%s", L("breakdown.raw_tooltip_crit2"));
            ImGui::EndTooltip();
        }

        // Render grouped spell rows
        size_t displayIndex = 0;
        for (size_t i = 0; i < groupedSpellRows_.size(); ++i) {
            auto& row = groupedSpellRows_[i];
            ImGui::PushID(static_cast<int>(i));

            if (row.type == GroupedSpellRow::Type::GroupHeader) {
                renderGroupHeader(row, iconLoader, needsRefresh);
            } else if (row.type == GroupedSpellRow::Type::PetGroupHeader) {
                renderPetGroupHeader(row);
            } else {
                renderSpellRow(row, iconLoader, displayIndex, selectedSpellIndex, actorGuid, needsRefresh);
                ++displayIndex;
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    // Blacklisted spells section (skip for Blacklist actor)
    if (!isBlacklistActor) {
        renderBlacklistedSection(iconLoader, needsRefresh);
    }
}

void BreakdownSpellTableRenderer::renderGroupingControls(bool& needsRefresh) {
    // "Group Spells" button to create a new group.
    if (awlui::Button(L("breakdown.group_spells"), awlui::ButtonVariant::Secondary)) {
        auto& specGroups = spellGroupSettings_.getOrCreateSpecGroups(currentSpecId_);
        specGroups.createGroup("New Group");
        spellGroupSettings_.saveToSettings();
        needsGroupRebuild_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("breakdown.group_tooltip"));
    }

    // Drop target for ungrouping (drop here to ungroup)
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPELL_ID")) {
            uint32_t spell_id = *static_cast<const uint32_t*>(payload->Data);
            auto& specGroups = spellGroupSettings_.getOrCreateSpecGroups(currentSpecId_);
            specGroups.removeSpellFromGroup(spell_id);
            spellGroupSettings_.saveToSettings();
            needsGroupRebuild_ = true;
            needsRefresh = true;
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::Separator();
}

void BreakdownSpellTableRenderer::renderGroupHeader(GroupedSpellRow& row, [[maybe_unused]] awow::ISpellIconRenderer* iconLoader,
                                                     bool& needsRefresh) {
    ImGui::TableNextRow();

    // Get spec groups for collapse toggle
    auto* specGroups = const_cast<spell_grouping::SpecSpellGroups*>(
        spellGroupSettings_.getSpecGroups(currentSpecId_));
    spell_grouping::SpellGroup* group = specGroups ? specGroups->findGroupById(row.group_id) : nullptr;

    // Column 1: Collapse arrow
    ImGui::TableNextColumn();
    bool collapsed = group ? group->collapsed : false;
    const char* arrow = collapsed ? ">" : "v";
    if (awlui::Button(arrow, awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
        if (group) {
            group->collapsed = !group->collapsed;
            spellGroupSettings_.saveToSettings();
            needsGroupRebuild_ = true;
        }
    }

    // Column 2: Empty (icon column)
    ImGui::TableNextColumn();

    // Column 3: Group name (editable on double-click)
    ImGui::TableNextColumn();

    // Check if we're editing this group's name
    if (isEditingGroupName_ && editingGroupId_ == row.group_id) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##GroupName", groupNameBuffer_, sizeof(groupNameBuffer_),
                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            // Save the new name
            if (group && strlen(groupNameBuffer_) > 0) {
                group->name = groupNameBuffer_;
                spellGroupSettings_.saveToSettings();
            }
            isEditingGroupName_ = false;
            editingGroupId_ = 0;
        }
        // Cancel on escape or click elsewhere
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0))) {
            isEditingGroupName_ = false;
            editingGroupId_ = 0;
        }
        // Auto-focus the input
        if (ImGui::IsItemVisible() && !ImGui::IsItemActive()) {
            ImGui::SetKeyboardFocusHere(-1);
        }
    } else {
        // Display group name - double-click to edit
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "%s", row.group_name.c_str());
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            isEditingGroupName_ = true;
            editingGroupId_ = row.group_id;
            strncpy(groupNameBuffer_, row.group_name.c_str(), sizeof(groupNameBuffer_) - 1);
            groupNameBuffer_[sizeof(groupNameBuffer_) - 1] = '\0';
        }

        // Context menu for delete
        if (ImGui::BeginPopupContextItem("GroupContextMenu")) {
            if (ImGui::MenuItem(L("breakdown.delete_group"))) {
                auto& groups = spellGroupSettings_.getOrCreateSpecGroups(currentSpecId_);
                groups.deleteGroup(row.group_id);
                spellGroupSettings_.saveToSettings();
                needsGroupRebuild_ = true;
                needsRefresh = true;
            }
            if (ImGui::MenuItem(L("breakdown.rename"))) {
                isEditingGroupName_ = true;
                editingGroupId_ = row.group_id;
                strncpy(groupNameBuffer_, row.group_name.c_str(), sizeof(groupNameBuffer_) - 1);
                groupNameBuffer_[sizeof(groupNameBuffer_) - 1] = '\0';
            }
            ImGui::EndPopup();
        }
    }

    // Drop target for adding spells to this group
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPELL_ID")) {
            uint32_t spell_id = *static_cast<const uint32_t*>(payload->Data);
            auto& groups2 = spellGroupSettings_.getOrCreateSpecGroups(currentSpecId_);
            groups2.addSpellToGroup(row.group_id, spell_id);
            spellGroupSettings_.saveToSettings();
            needsGroupRebuild_ = true;
            needsRefresh = true;
        }
        ImGui::EndDragDropTarget();
    }

    // Column 4: Group total
    ImGui::TableNextColumn();
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "%s",
                       ui::format::formatAmount(row.group_total).c_str());

    // Column 5: Group effective (empty for groups - would need to sum effective amounts)
    ImGui::TableNextColumn();

    // Column 6: Percent of actor's total
    ImGui::TableNextColumn();
    float percent = (actorStats_ && actorStats_->total_amount > 0)
        ? static_cast<float>(row.group_total) / static_cast<float>(actorStats_->total_amount) * 100.0f
        : 0.0f;
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "%.1f%%", percent);

    // Column 7, 8 & 9: Empty for groups (Over%, Crit%, Hits)
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
}

void BreakdownSpellTableRenderer::renderPetGroupHeader(GroupedSpellRow& row) {
    ImGui::TableNextRow();

    std::string key = petGroupKey(row.pet_npc_id, row.pet_guid);
    bool collapsed = collapsedPetGroups_.find(key) != collapsedPetGroups_.end();

    // Column 1: Collapse arrow (toggle-only; pet groups aren't user-editable)
    ImGui::TableNextColumn();
    const char* arrow = collapsed ? ">" : "v";
    if (awlui::Button(arrow, awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
        if (collapsed) {
            collapsedPetGroups_.erase(key);
        } else {
            collapsedPetGroups_.insert(key);
        }
        needsGroupRebuild_ = true;
    }

    // Column 2: Empty (icon column) - overlay has no icon loader, so pet
    // group headers stay text-only.
    ImGui::TableNextColumn();

    // Column 3: Pet type name, resolved from the representative guid.
    ImGui::TableNextColumn();
    const std::string* displayName = &row.pet_guid;
    if (petGroupNames_) {
        auto it = petGroupNames_->find(row.pet_guid);
        if (it != petGroupNames_->end() && !it->second.empty()) {
            displayName = &it->second;
        }
    }
    // Bracketed to read like a pet-type header (e.g. "[Lesser Ghoul]").
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "[%s]", displayName->c_str());

    // Column 4: Group total
    ImGui::TableNextColumn();
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "%s",
                       ui::format::formatAmount(row.group_total).c_str());

    // Column 5: Effective (blank for groups)
    ImGui::TableNextColumn();

    // Column 6: Percent of actor's total
    ImGui::TableNextColumn();
    float percent = (actorStats_ && actorStats_->total_amount > 0)
        ? static_cast<float>(row.group_total) / static_cast<float>(actorStats_->total_amount) * 100.0f
        : 0.0f;
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "%.1f%%", percent);

    // Columns 7, 8 & 9: empty for groups
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
}

void BreakdownSpellTableRenderer::renderSpellRow(GroupedSpellRow& row, awow::ISpellIconRenderer* iconLoader,
                                                  size_t displayIndex, int& selectedSpellIndex,
                                                  const std::string& actorGuid, bool& needsRefresh) {
    if (!row.spell || !actorStats_) return;

    const auto& spell = *row.spell;
    const float iconSize = 20.0f;
    bool isGrouped = (row.group_id > 0);

    // Find the index in spell_breakdown for selection tracking
    int spellIndex = -1;
    for (size_t i = 0; i < actorStats_->spell_breakdown.size(); ++i) {
        if (&actorStats_->spell_breakdown[i] == row.spell) {
            spellIndex = static_cast<int>(i);
            break;
        }
    }
    bool isSelected = (selectedSpellIndex == spellIndex);

    ImGui::TableNextRow();

    // Column 1: Rank/indent (with selectable)
    ImGui::TableNextColumn();

    // Indent grouped spells
    if (isGrouped) {
        ImGui::Indent(10.0f);
    }

    std::string label = isGrouped ? "-" : std::to_string(displayIndex + 1);
    if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
        selectedSpellIndex = spellIndex;
    }

    // Context menu for blacklisting/unblacklisting
    if (ImGui::BeginPopupContextItem("SpellContextMenu")) {
        if (spell.spell_id == 1) {
            ImGui::Text("%s", L("meter.melee"));
        } else {
            ImGui::Text(L("format.spell_id"), spell.spell_id);
        }
        ImGui::Separator();

        // Show "Unblacklist" for Blacklist actor, "Blacklist" for normal actors
        bool isBlacklistActor = (actorGuid == BLACKLIST_ACTOR_GUID);
        if (isBlacklistActor) {
            if (ImGui::MenuItem(L("breakdown.unblacklist_spell"))) {
                spellGroupSettings_.unblacklistSpell(spell.spell_id);
                spellGroupSettings_.saveToSettings();
                needsGroupRebuild_ = true;
                needsRefresh = true;
            }
        } else {
            if (ImGui::MenuItem(L("breakdown.blacklist_spell"))) {
                spellGroupSettings_.blacklistSpell(spell.spell_id);
                spellGroupSettings_.saveToSettings();
                needsGroupRebuild_ = true;
                needsRefresh = true;
            }
        }

        // Phase trigger toggle: the first cast of the marked spell
        // starts a new phase in the meters for this encounter. Only
        // offered while a boss encounter is loaded (melee row has no
        // real spell id to key on). The request funnels through the
        // same owner-side handling as the spell-icon context menu.
        uint32_t phaseEncounterId = SpellContextMenu::getCurrentEncounterId();
        if (phaseEncounterId != 0 && spell.spell_id > 1) {
            bool isTrigger = PhaseSettings::instance().hasRule(phaseEncounterId, spell.spell_id);
            const char* phaseLabel = isTrigger ? L("context_menu.remove_phase_trigger")
                                               : L("context_menu.set_phase_trigger");
            if (ImGui::MenuItem(phaseLabel)) {
                SpellContextMenu::requestPhaseToggle(spell.spell_id, !isTrigger);
            }
        }
        ImGui::EndPopup();
    }

    // Drag source for moving spells between groups
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        uint32_t spell_id = spell.spell_id;
        ImGui::SetDragDropPayload("SPELL_ID", &spell_id, sizeof(uint32_t));

        // Preview: show spell ID being dragged
        if (spell.spell_id == 1) {
            ImGui::Text("%s", L("breakdown.dragging_melee"));
        } else {
            ImGui::Text(L("breakdown.dragging_spell"), spell.spell_id);
        }
        ImGui::EndDragDropSource();
    }

    if (isGrouped) {
        ImGui::Unindent(10.0f);
    }

    // Column 2: Icon with legend color swatch
    ImGui::TableNextColumn();

    // Draw legend color swatch next to icon (same size as icon)
    ImU32 legendColor = getSpellColor(spell.spell_id);
    ImVec2 swatchPos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        swatchPos,
        ImVec2(swatchPos.x + iconSize, swatchPos.y + iconSize),
        legendColor,
        2.0f  // Rounded corners
    );
    ImGui::Dummy(ImVec2(iconSize + 2.0f, iconSize));
    ImGui::SameLine(0, 0);

    if (iconLoader) {
        iconLoader->renderSpellIcon(spell.spell_id, ImVec2(iconSize, iconSize));
    }

    // Column 3: Spell Name (or "Melee")
    ImGui::TableNextColumn();
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

    // Hit distribution tooltip: normal/crit/glance/resist/block/absorb
    // buckets with count/total/min/max. Helps identify wasted casts and
    // damage lost to mitigation - Details' per-spell drill-down.
    if (ImGui::IsItemHovered()) {
        auto renderBucket = [](const char* label, uint32_t count, int64_t total,
                               int64_t minv, int64_t maxv) {
            if (count == 0) return;
            int64_t safeMin = (minv == INT64_MAX) ? 0 : minv;
            int64_t avg = count > 0 ? total / static_cast<int64_t>(count) : 0;
            std::string totalStr = ui::format::formatAmount(total);
            std::string avgStr = ui::format::formatAmount(avg);
            std::string minStr = ui::format::formatAmount(safeMin);
            std::string maxStr = ui::format::formatAmount(maxv);
            ImGui::Text("  %s: %u hits, total %s, avg %s (%s-%s)",
                label, count, totalStr.c_str(), avgStr.c_str(),
                minStr.c_str(), maxStr.c_str());
        };
        ImGui::BeginTooltip();
        ImGui::Text("%s", L("breakdown.hit_distribution"));
        ImGui::Separator();
        renderBucket(L("breakdown.hit_normal"), spell.normal_count, spell.normal_total,
                     spell.normal_min, spell.normal_max);
        renderBucket(L("breakdown.hit_crit"), spell.crit_count, spell.crit_total,
                     spell.crit_min, spell.crit_max);
        renderBucket(L("breakdown.hit_glance"), spell.glance_hits.count,
                     spell.glance_hits.total, spell.glance_hits.min, spell.glance_hits.max);
        renderBucket(L("breakdown.hit_resisted"), spell.resisted_hits.count,
                     spell.resisted_hits.total, spell.resisted_hits.min, spell.resisted_hits.max);
        renderBucket(L("breakdown.hit_blocked"), spell.blocked_hits.count,
                     spell.blocked_hits.total, spell.blocked_hits.min, spell.blocked_hits.max);
        renderBucket(L("breakdown.hit_absorbed"), spell.absorbed_hits.count,
                     spell.absorbed_hits.total, spell.absorbed_hits.min, spell.absorbed_hits.max);
        ImGui::EndTooltip();
    }

    // Column 4: Total amount (pre-mitigation)
    ImGui::TableNextColumn();
    ImGui::Text("%s", ui::format::formatAmount(spell.total_amount).c_str());

    // Column 5: Effective amount (post-mitigation)
    ImGui::TableNextColumn();
    ImGui::Text("%s", ui::format::formatAmount(spell.effective_amount).c_str());

    // Column 6: Percent of actor's total (based on effective)
    ImGui::TableNextColumn();
    float percent = (actorStats_->effective_amount > 0)
        ? static_cast<float>(spell.effective_amount) / static_cast<float>(actorStats_->effective_amount) * 100.0f
        : 0.0f;
    ImGui::Text("%.1f%%", percent);

    // Column 7: Overheal (healing) / Mitigation (damage) percent
    ImGui::TableNextColumn();
    int64_t wastedAmount = spell.total_amount - spell.effective_amount;
    float wastedPercent = (spell.total_amount > 0)
        ? static_cast<float>(wastedAmount) / static_cast<float>(spell.total_amount) * 100.0f
        : 0.0f;
    if (wastedPercent > 0.0f) {
        // Color based on how much is wasted/mitigated (green = low, yellow = medium, red = high)
        ImVec4 wastedColor;
        if (wastedPercent < 10.0f) {
            wastedColor = ImVec4(0.4f, 0.8f, 0.4f, 1.0f);  // Green
        } else if (wastedPercent < 30.0f) {
            wastedColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);  // Yellow
        } else {
            wastedColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);  // Red
        }
        ImGui::TextColored(wastedColor, "%.1f%%", wastedPercent);
    } else {
        ImGui::TextDisabled("-");
    }

    // Column 8: Crit percent
    ImGui::TableNextColumn();
    float spellCritPercent = (spell.hit_count > 0)
        ? static_cast<float>(spell.crit_count) / static_cast<float>(spell.hit_count) * 100.0f
        : 0.0f;
    ImGui::Text("%.1f%%", spellCritPercent);

    // Column 9: Hit count
    ImGui::TableNextColumn();
    ImGui::Text("%u", spell.hit_count);
}

void BreakdownSpellTableRenderer::renderBlacklistedSection(awow::ISpellIconRenderer* iconLoader, bool& needsRefresh) {
    if (blacklistedSpellRows_.empty()) {
        return;
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Collapsible header for blacklisted spells
    bool isHealing = (metricType_ == CombatMetricType::HealingDone);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    bool expanded = ImGui::CollapsingHeader(isHealing ? L("breakdown.blacklisted_healing")
                                                       : L("breakdown.blacklisted_damage"));
    ImGui::PopStyleColor();

    if (!expanded) {
        return;
    }

    const float iconSize = 18.0f;

    // Simple table for blacklisted spells
    ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;

    if (ImGui::BeginTable("BlacklistedTable", 5, tableFlags)) {
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 25.0f);
        ImGui::TableSetupColumn("Spell", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(isHealing ? "Healing" : "Damage", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Hits", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < blacklistedSpellRows_.size(); ++i) {
            const auto* spell = blacklistedSpellRows_[i];
            if (!spell) continue;

            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i + 10000));  // Offset to avoid ID collision

            // Icon
            ImGui::TableNextColumn();
            if (iconLoader) {
                iconLoader->renderSpellIcon(spell->spell_id, ImVec2(iconSize, iconSize));
            }

            // Spell ID
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            if (spell->spell_id == 1) {
                ImGui::Text("%s", L("meter.melee"));
            } else {
                ImGui::Text("%u", spell->spell_id);
            }
            ImGui::PopStyleColor();

            // Damage (grayed out)
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::Text("%s", ui::format::formatAmount(spell->total_amount).c_str());
            ImGui::PopStyleColor();

            // Hits
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::Text("%u", spell->hit_count);
            ImGui::PopStyleColor();

            // Unblacklist button
            ImGui::TableNextColumn();
            if (awlui::Button(L("breakdown.restore"), awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
                spellGroupSettings_.unblacklistSpell(spell->spell_id);
                spellGroupSettings_.saveToSettings();
                needsGroupRebuild_ = true;
                needsRefresh = true;
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

ImU32 BreakdownSpellTableRenderer::getSpellColor(uint32_t spellId) const {
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
