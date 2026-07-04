#include "UI/Panels/Combat/UIMeterPanel.h"
#include "UI/ISpellIconRenderer.h"
#include "UI/AwlUI/Widgets.h"
#include "Color/ActorColorGenerator.h"
#include "Core/NumberFormatter.h"
#include "Core/UIUtils.h"
#include "Core/LocalizationManager.h"
#include "Core/UnifiedSettings.h"
#include "CombatDatabase.h"
#include "DeathDatabase.h"
#include "ResourceDatabase.h"
#include "AuraDatabase.h"
#include "AvoidanceDatabase.h"
#include "AbsorbDatabase.h"
#include "imgui.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdio>

UIMeterPanel::UIMeterPanel(int instanceId)
    : instanceId_(instanceId)
{
    // Generate unique window title based on instance
    // Uses localized string "panel.meter_N" where N is 1, 2, or 3
    switch (instanceId_) {
        case 1: windowTitle_ = "panel.meter_1"; break;
        case 2: windowTitle_ = "panel.meter_2"; break;
        case 3: windowTitle_ = "panel.meter_3"; break;
        default: windowTitle_ = "panel.meter_1"; break;
    }

    // Unique tab bar ID per instance
    tabBarId_ = "MeterTabs##" + std::to_string(instanceId_);

    // Default configuration
    config_.view_type = MeterViewType::DamageDealt;
    config_.time_mode = MeterTimeMode::Cumulative;
    config_.show_percent = true;
    config_.show_amount_per_second = true;
    config_.bar_height = 20.0f;
    config_.bar_spacing = 2.0f;

    // Load spell group settings for blacklist
    spellGroupSettings_.loadFromSettings();
}

bool UIMeterPanel::render(
    const CombatDatabase* combatDb,
    const DispelDatabase* dispelDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    const DeathDatabase* deathDb,
    const ResourceDatabase* /*resourceDb*/,
    const AuraDatabase* auraDb,
    const AvoidanceDatabase* avoidanceDb
) {
    if (!visible_) {
        return false;
    }

    // Create window with localized title
    ImGui::Begin(L(windowTitle_.c_str()), &visible_);

    // Render tab bar for view selection
    renderTabBar();

    ImGui::Separator();

    // Render time mode selector for all views (Cumulative vs Full Encounter)
    renderTimeModeSelector(guidToName, combatGuidToName);

    // Remind the user when mob weighting is shaping the numbers
    if (combatDb && combatDb->hasActiveTargetWeights()) {
        ImGui::TextColored(awlui::theme::Accent, "%s", L("mobweight.active_hint"));
    }
    ImGui::Separator();

    // Render the selected view
    switch (config_.view_type) {
        case MeterViewType::DamageDealt:
            renderDamageView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::HealingDone:
            renderHealingView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::DamageTaken:
        case MeterViewType::DamageTakenByAbility:
            renderDamageTakenView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::DamageTakenBy:
            renderDamageTakenByView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Dispels:
            renderDispelView(dispelDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Interrupts:
            renderInterruptView(dispelDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Overhealing:
            renderOverhealingView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::HealingTaken:
            renderHealingTakenView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::FriendlyFire:
            renderFriendlyFireView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::CCBreaks:
            renderCCBreaksView(auraDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Deaths:
            renderDeathsView(deathDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Avoidance:
            renderAvoidanceView(avoidanceDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Absorbs:
            ImGui::Text("%s", L("status.no_absorb_data"));
            break;
        case MeterViewType::EnemyDamage:
            renderEnemyDamageView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::EnemyHealing:
            renderEnemyHealingView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
    }

    renderMeterOptionsMenu();

    ImGui::End();
    return visible_;
}

void UIMeterPanel::renderEmbedded(
    const CombatDatabase* combatDb,
    const DispelDatabase* dispelDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    const DeathDatabase* deathDb,
    const ResourceDatabase* /*resourceDb*/,
    const AuraDatabase* auraDb,
    const AbsorbDatabase* absorbDb
) {
    // Render tab bar for view selection
    renderTabBar();

    ImGui::Separator();

    // Render time mode selector for all views (Cumulative vs Full Encounter)
    renderTimeModeSelector(guidToName, combatGuidToName);

    // Remind the user when mob weighting is shaping the numbers
    if (combatDb && combatDb->hasActiveTargetWeights()) {
        ImGui::TextColored(awlui::theme::Accent, "%s", L("mobweight.active_hint"));
    }
    ImGui::Separator();

    // Render the selected view
    switch (config_.view_type) {
        case MeterViewType::DamageDealt:
            renderDamageView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::HealingDone:
            renderHealingView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::DamageTaken:
        case MeterViewType::DamageTakenByAbility:
            renderDamageTakenView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::DamageTakenBy:
            renderDamageTakenByView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Dispels:
            renderDispelView(dispelDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Interrupts:
            renderInterruptView(dispelDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Overhealing:
            renderOverhealingView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::HealingTaken:
            renderHealingTakenView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::FriendlyFire:
            renderFriendlyFireView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::CCBreaks:
            renderCCBreaksView(auraDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Deaths:
            renderDeathsView(deathDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Avoidance:
            // Note: Avoidance not available in embedded mode (no avoidanceDb param)
            ImGui::Text("%s", L("status.no_avoidance_data"));
            break;
        case MeterViewType::Absorbs:
            renderAbsorbsView(absorbDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::EnemyDamage:
            renderEnemyDamageView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::EnemyHealing:
            renderEnemyHealingView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
    }
}

static const char* getMeterViewName(MeterViewType type);  // forward decl

void UIMeterPanel::renderEmbeddedContent(
    const CombatDatabase* combatDb,
    const DispelDatabase* dispelDb,
    const std::shared_ptr<ActorColorGenerator>& colorGen,
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    uint32_t currentTime_ms,
    awow::ISpellIconRenderer* iconLoader,
    const std::unordered_map<std::string, std::string>* combatGuidToName,
    const DeathDatabase* deathDb,
    const ResourceDatabase* /*resourceDb*/,
    const AuraDatabase* auraDb,
    const AbsorbDatabase* absorbDb,
    const AvoidanceDatabase* avoidanceDb,
    int32_t filterStartTime_ms,
    int32_t filterEndTime_ms,
    const std::unordered_map<uint32_t, std::string>* spellNameFallback
) {
    // Store the filter times for use by render functions
    filterStartTime_ms_ = filterStartTime_ms;
    filterEndTime_ms_ = filterEndTime_ms;
    currentSpellNameFallback_ = spellNameFallback;

    // A selected phase narrows the segment window the caller passed
    // in. Re-applied every frame because the assignment above resets
    // the filter to the full segment.
    if (showPhaseControls_ && selectedPhase_ >= 0 &&
        selectedPhase_ < static_cast<int>(phases_.size())) {
        const auto& phase = phases_[selectedPhase_];
        // The views treat 0 as "no filter", so nudge a phase starting
        // at the log origin to 1ms
        filterStartTime_ms_ = (phase.start_ms > 0) ? phase.start_ms : 1;
        filterEndTime_ms_ = phase.end_ms;
    }

    // Compact per-panel view selector at the top of the embedded content
    // so users can switch this meter's view without touching the shared
    // overlay header. Each panel keeps its own view_type in config_.
    {
        static constexpr MeterViewType kAllViews[] = {
            MeterViewType::DamageDealt, MeterViewType::HealingDone,
            MeterViewType::DamageTaken, MeterViewType::DamageTakenBy,
            MeterViewType::FriendlyFire, MeterViewType::EnemyDamage,
            MeterViewType::Overhealing, MeterViewType::HealingTaken,
            MeterViewType::Absorbs, MeterViewType::EnemyHealing,
            MeterViewType::Dispels, MeterViewType::Interrupts,
            MeterViewType::CCBreaks, MeterViewType::Deaths,
            MeterViewType::Avoidance,
        };
        constexpr int kNumViews = static_cast<int>(sizeof(kAllViews) / sizeof(kAllViews[0]));

        // Build the label array + resolve the currently-selected
        // index for awlui::Combo. Labels come out of the localization
        // system so they can't be constexpr, but the mapping is
        // stable for the frame.
        const char* labels[kNumViews];
        int currentIdx = 0;
        for (int i = 0; i < kNumViews; ++i) {
            labels[i] = getMeterViewName(kAllViews[i]);
            if (kAllViews[i] == config_.view_type) currentIdx = i;
        }

        std::string comboId = "##ViewType" + std::to_string(instanceId_);
        int newIdx = currentIdx;
        if (awlui::Combo(comboId.c_str(), &newIdx, labels, kNumViews)) {
            config_.view_type = kAllViews[newIdx];
        }

        // Phase selector + editor button on their own row - only for
        // boss segments (the owner flips showPhaseControls_ off when
        // the segment has no encounter id). The overlay draws these in
        // its own top header row instead, so it suppresses the in-body
        // copy via phaseControlsExternal_.
        if (showPhaseControls_ && !phaseControlsExternal_) {
            renderPhaseControls();
        }

        // Remind the user when mob weighting is shaping the numbers -
        // this is the path the overlay renders through
        if (combatDb && combatDb->hasActiveTargetWeights()) {
            ImGui::TextColored(awlui::theme::Accent, "%s", L("mobweight.active_hint"));
        }
        ImGui::Separator();
    }

    // Render only the view content - no tab bar or time selector
    // (The overlay header handles meter type and scope selection)
    switch (config_.view_type) {
        case MeterViewType::DamageDealt:
            renderDamageView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::HealingDone:
            renderHealingView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::DamageTaken:
        case MeterViewType::DamageTakenByAbility:
            renderDamageTakenView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::DamageTakenBy:
            renderDamageTakenByView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Dispels:
            renderDispelView(dispelDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Interrupts:
            renderInterruptView(dispelDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Overhealing:
            renderOverhealingView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::HealingTaken:
            renderHealingTakenView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::FriendlyFire:
            renderFriendlyFireView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::CCBreaks:
            renderCCBreaksView(auraDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Deaths:
            renderDeathsView(deathDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Avoidance:
            renderAvoidanceView(avoidanceDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::Absorbs:
            renderAbsorbsView(absorbDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::EnemyDamage:
            renderEnemyDamageView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
        case MeterViewType::EnemyHealing:
            renderEnemyHealingView(combatDb, colorGen, guidToName, currentTime_ms, iconLoader, combatGuidToName);
            break;
    }

    renderMeterOptionsMenu();
}

// Meter category for grouping
enum class MeterCategory { Damage, Healing, Utility, Survivability };

// Get category color for visual distinction
static ImVec4 getCategoryColor(MeterCategory cat) {
    switch (cat) {
        case MeterCategory::Damage:       return ImVec4(0.85f, 0.35f, 0.35f, 1.0f);  // Red
        case MeterCategory::Healing:      return ImVec4(0.35f, 0.75f, 0.45f, 1.0f);  // Green
        case MeterCategory::Utility:      return ImVec4(0.45f, 0.65f, 0.85f, 1.0f);  // Blue
        case MeterCategory::Survivability: return ImVec4(0.75f, 0.55f, 0.35f, 1.0f); // Orange
        default: return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    }
}

// Get category for a meter type
static MeterCategory getMeterCategory(MeterViewType type) {
    switch (type) {
        case MeterViewType::DamageDealt:
        case MeterViewType::DamageTaken:
        case MeterViewType::DamageTakenByAbility:
        case MeterViewType::DamageTakenBy:
        case MeterViewType::FriendlyFire:
        case MeterViewType::EnemyDamage:
            return MeterCategory::Damage;
        case MeterViewType::HealingDone:
        case MeterViewType::Overhealing:
        case MeterViewType::HealingTaken:
        case MeterViewType::Absorbs:
        case MeterViewType::EnemyHealing:
            return MeterCategory::Healing;
        case MeterViewType::Dispels:
        case MeterViewType::Interrupts:
        case MeterViewType::CCBreaks:
            return MeterCategory::Utility;
        case MeterViewType::Deaths:
        case MeterViewType::Avoidance:
            return MeterCategory::Survivability;
        default:
            return MeterCategory::Damage;
    }
}

// Helper to get display name for a meter view type
static const char* getMeterViewName(MeterViewType type) {
    switch (type) {
        case MeterViewType::DamageDealt: return L("meter.tab.damage");
        case MeterViewType::HealingDone: return L("meter.tab.healing");
        case MeterViewType::DamageTaken: return L("meter.tab.taken");
        case MeterViewType::DamageTakenByAbility: return L("meter.tab.taken");
        case MeterViewType::DamageTakenBy: return L("meter.tab.taken_by");
        case MeterViewType::Dispels: return L("meter.tab.dispels");
        case MeterViewType::Interrupts: return L("meter.tab.interrupts");
        case MeterViewType::Overhealing: return L("meter.tab.overhealing");
        case MeterViewType::HealingTaken: return L("meter.tab.healing_taken");
        case MeterViewType::FriendlyFire: return L("meter.tab.friendly_fire");
        case MeterViewType::CCBreaks: return L("meter.tab.cc_breaks");
        case MeterViewType::Deaths: return L("meter.tab.deaths");
        case MeterViewType::Avoidance: return L("meter.tab.avoidance");
        case MeterViewType::Absorbs: return L("meter.tab.absorbs");
        case MeterViewType::EnemyDamage: return L("meter.tab.enemy_damage");
        case MeterViewType::EnemyHealing: return L("meter.tab.enemy_healing");
        default: return "Unknown";
    }
}

// Helper to render a colored dot indicator
static void renderCategoryDot(MeterCategory cat) {
    ImVec4 color = getCategoryColor(cat);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float radius = 4.0f;
    pos.x += radius + 2.0f;
    pos.y += ImGui::GetTextLineHeight() * 0.5f;
    ImGui::GetWindowDrawList()->AddCircleFilled(pos, radius, ImGui::ColorConvertFloat4ToU32(color));
    ImGui::Dummy(ImVec2(radius * 2 + 6.0f, 0));
    ImGui::SameLine();
}

// Render a selectable meter item with category dot
// Returns a one-line description of what the view shows. Rendered as
// a hover tooltip on the meter-item entry so users don't have to guess
// which of the similarly-named views (Damage Taken vs Enemy Damage
// vs Damage Taken By) does what.
static const char* getMeterViewTooltip(MeterViewType type) {
    switch (type) {
        case MeterViewType::DamageDealt:         return "Damage each of your players did.";
        case MeterViewType::HealingDone:         return "Healing each of your players cast.";
        case MeterViewType::DamageTaken:         return "Damage each of your players received. Groups by target (the victim).";
        case MeterViewType::DamageTakenByAbility:return "Damage taken, broken down by the ability that hit you.";
        case MeterViewType::DamageTakenBy:       return "Pick an enemy - shows who on your team dealt damage to that enemy.";
        case MeterViewType::Dispels:             return "Successful dispels/purges each of your players cast.";
        case MeterViewType::Interrupts:          return "Successful interrupts each of your players landed.";
        case MeterViewType::Overhealing:         return "Wasted healing (excess beyond target's max HP).";
        case MeterViewType::HealingTaken:        return "Healing each of your players received. Click a row for who healed them.";
        case MeterViewType::FriendlyFire:        return "Damage each of your players dealt to allies (bad).";
        case MeterViewType::CCBreaks:            return "Crowd control your players broke early.";
        case MeterViewType::Deaths:              return "Deaths of your players. Click a row for a full post-mortem.";
        case MeterViewType::Avoidance:           return "Damage each of your players avoided (dodge/parry/block/absorb).";
        case MeterViewType::Absorbs:             return "Damage each of your players absorbed with shields.";
        case MeterViewType::EnemyDamage:         return "Damage each hostile actor (boss/adds) dealt to your team.";
        case MeterViewType::EnemyHealing:        return "Healing each hostile actor cast (usually boss self-heals).";
        default:                                 return nullptr;
    }
}

static bool renderMeterItem(const char* label, MeterViewType type, MeterViewType& currentType) {
    bool isSelected = (currentType == type);

    // Indent items under category headers
    ImGui::Indent(8.0f);

    // Add colored dot
    renderCategoryDot(getMeterCategory(type));

    // Highlight selected item
    if (isSelected) {
        ImGui::PushStyleColor(ImGuiCol_Text, getCategoryColor(getMeterCategory(type)));
    }

    bool clicked = ImGui::Selectable(label, isSelected);

    if (isSelected) {
        ImGui::PopStyleColor();
    }

    // Hover tooltip explaining the view - critical for disambiguating
    // similarly-named entries (Damage Taken vs Enemy Damage etc).
    if (ImGui::IsItemHovered()) {
        const char* tip = getMeterViewTooltip(type);
        if (tip) ImGui::SetTooltip("%s", tip);
    }

    ImGui::Unindent(8.0f);

    if (clicked) {
        currentType = type;
        return true;
    }
    return false;
}

void UIMeterPanel::renderTabBar() {
    // Get current category color for the preview
    MeterCategory currentCat = getMeterCategory(config_.view_type);
    ImVec4 catColor = getCategoryColor(currentCat);

    // Style the combo box
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.10f, 0.10f, 0.13f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(catColor.x * 0.6f, catColor.y * 0.6f, catColor.z * 0.6f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f);

    ImGui::PushItemWidth(-1);  // Full width

    std::string comboId = "##MeterSelect" + std::to_string(instanceId_);

    // Build preview string with category dot
    std::string previewText = getMeterViewName(config_.view_type);

    if (ImGui::BeginCombo(comboId.c_str(), previewText.c_str())) {
        // Damage category
        ImGui::PushStyleColor(ImGuiCol_Text, getCategoryColor(MeterCategory::Damage));
        ImGui::Text("%s", L("meter.category.damage"));
        ImGui::PopStyleColor();
        ImGui::Separator();

        renderMeterItem(L("meter.tab.damage"), MeterViewType::DamageDealt, config_.view_type);
        renderMeterItem(L("meter.tab.taken"), MeterViewType::DamageTaken, config_.view_type);
        renderMeterItem(L("meter.tab.taken_by"), MeterViewType::DamageTakenBy, config_.view_type);
        renderMeterItem(L("meter.tab.friendly_fire"), MeterViewType::FriendlyFire, config_.view_type);
        renderMeterItem(L("meter.tab.enemy_damage"), MeterViewType::EnemyDamage, config_.view_type);

        ImGui::Spacing();
        ImGui::Spacing();

        // Healing category
        ImGui::PushStyleColor(ImGuiCol_Text, getCategoryColor(MeterCategory::Healing));
        ImGui::Text("%s", L("meter.category.healing"));
        ImGui::PopStyleColor();
        ImGui::Separator();

        renderMeterItem(L("meter.tab.healing"), MeterViewType::HealingDone, config_.view_type);
        renderMeterItem(L("meter.tab.overhealing"), MeterViewType::Overhealing, config_.view_type);
        renderMeterItem(L("meter.tab.healing_taken"), MeterViewType::HealingTaken, config_.view_type);
        renderMeterItem(L("meter.tab.absorbs"), MeterViewType::Absorbs, config_.view_type);
        renderMeterItem(L("meter.tab.enemy_healing"), MeterViewType::EnemyHealing, config_.view_type);

        ImGui::Spacing();
        ImGui::Spacing();

        // Utility category
        ImGui::PushStyleColor(ImGuiCol_Text, getCategoryColor(MeterCategory::Utility));
        ImGui::Text("%s", L("meter.category.utility"));
        ImGui::PopStyleColor();
        ImGui::Separator();

        renderMeterItem(L("meter.tab.dispels"), MeterViewType::Dispels, config_.view_type);
        renderMeterItem(L("meter.tab.interrupts"), MeterViewType::Interrupts, config_.view_type);
        renderMeterItem(L("meter.tab.cc_breaks"), MeterViewType::CCBreaks, config_.view_type);

        ImGui::Spacing();
        ImGui::Spacing();

        // Survivability category
        ImGui::PushStyleColor(ImGuiCol_Text, getCategoryColor(MeterCategory::Survivability));
        ImGui::Text("%s", L("meter.category.survivability"));
        ImGui::PopStyleColor();
        ImGui::Separator();

        renderMeterItem(L("meter.tab.deaths"), MeterViewType::Deaths, config_.view_type);
        renderMeterItem(L("meter.tab.avoidance"), MeterViewType::Avoidance, config_.view_type);

        ImGui::EndCombo();
    }

    // Draw category indicator dot on the combo preview
    {
        ImVec2 comboMin = ImGui::GetItemRectMin();
        ImVec2 comboMax = ImGui::GetItemRectMax();
        float dotRadius = 4.0f;
        ImVec2 dotPos(comboMin.x + 10.0f, (comboMin.y + comboMax.y) * 0.5f);
        ImGui::GetWindowDrawList()->AddCircleFilled(dotPos, dotRadius, ImGui::ColorConvertFloat4ToU32(catColor));
    }

    ImGui::PopItemWidth();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
}

void UIMeterPanel::renderTimeModeSelector(
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    const char* timeModeNames[] = { L("meter.cumulative"), L("meter.full_encounter") };
    int currentTimeMode = static_cast<int>(config_.time_mode);

    // Unique ID for combo per instance
    std::string comboId = "##TimeMode" + std::to_string(instanceId_);
    if (awlui::Combo(comboId.c_str(), &currentTimeMode, timeModeNames, 2)) {
        config_.time_mode = static_cast<MeterTimeMode>(currentTimeMode);
    }

    ImGui::SameLine();
    renderPhaseControls();

    ImGui::SameLine();
    renderReportButton(guidToName, combatGuidToName);
}

void UIMeterPanel::renderPhaseControls() {
    // Opens the phase editor panel. Lives outside the phases_ check so
    // encounters without any rules yet can still reach the editor.
    ImGui::PushID(instanceId_);
    if (awlui::IconButton("phase_editor", "+")) {
        phaseEditorRequested_ = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("phases.title"));
    }
    ImGui::PopID();

    // Phase filter - only when the current encounter has phase rules
    // that actually fired this pull
    if (!phases_.empty()) {
        std::vector<const char*> phaseNames;
        phaseNames.reserve(phases_.size() + 1);
        phaseNames.push_back(L("meter.phase_all"));
        for (const auto& phase : phases_) {
            phaseNames.push_back(phase.label.c_str());
        }

        int currentPhase = selectedPhase_ + 1;  // Slot 0 = all phases
        std::string phaseComboId = "##PhaseFilter" + std::to_string(instanceId_);
        ImGui::SameLine();
        if (awlui::Combo(phaseComboId.c_str(), &currentPhase,
                         phaseNames.data(),
                         static_cast<int>(phaseNames.size()))) {
            selectedPhase_ = currentPhase - 1;
            if (selectedPhase_ < 0) {
                filterStartTime_ms_ = 0;
                filterEndTime_ms_ = 0;
            } else {
                const auto& phase = phases_[selectedPhase_];
                // The views treat 0 as "no filter", so nudge a phase
                // starting at the log origin to 1ms
                filterStartTime_ms_ = (phase.start_ms > 0) ? phase.start_ms : 1;
                filterEndTime_ms_ = phase.end_ms;
            }
        }
    }
}

void UIMeterPanel::renderMeterOptionsMenu() {
    // Right-click on empty meter space (not on a bar or control) opens
    // the display options. Shared by the windowed panels and the
    // overlay's embedded path so both apps expose the same options.
    std::string popupId = "MeterOptions" + std::to_string(instanceId_);
    if (ImGui::BeginPopupContextWindow(popupId.c_str(),
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        auto& cache = SettingsCache::instance();
        bool classColors = cache.get().meterClassColors;
        if (awlui::Checkbox(L("meter.class_colors"), &classColors)) {
            cache.get().meterClassColors = classColors;
            cache.markDirty();
            // Write through immediately so the main app and overlay
            // stay in agreement even on an abrupt close
            cache.flush();
        }
        ImGui::EndPopup();
    }
}

void UIMeterPanel::setPhases(std::vector<MeterPhase> phases) {
    // The overlay re-pushes the resolved phases every frame, so only reset
    // the selection when the set of phase windows actually changed - a new
    // pull, an added rule, or a boundary shifting as its trigger fires.
    // Resetting unconditionally would snap the user's chosen phase back to
    // "all phases" on the very next frame, showing the whole encounter.
    const bool sameSet =
        phases.size() == phases_.size() &&
        std::equal(phases.begin(), phases.end(), phases_.begin(),
                   [](const MeterPhase& a, const MeterPhase& b) {
                       return a.start_ms == b.start_ms && a.end_ms == b.end_ms &&
                              a.label == b.label;
                   });

    phases_ = std::move(phases);

    if (!sameSet) {
        selectedPhase_ = -1;
        filterStartTime_ms_ = 0;
        filterEndTime_ms_ = 0;
    } else if (selectedPhase_ >= static_cast<int>(phases_.size())) {
        selectedPhase_ = -1;
    }
}

void UIMeterPanel::renderReportButton(
    const std::unordered_map<std::string_view, std::string_view>& guidToName,
    const std::unordered_map<std::string, std::string>* combatGuidToName
) {
    if (reportFeedbackTimer_ > 0.0f) {
        reportFeedbackTimer_ -= ImGui::GetIO().DeltaTime;
    }

    const char* buttonLabel = (reportFeedbackTimer_ > 0.0f)
        ? L("meter.report_copied")
        : L("meter.report");

    bool noData = cachedCombatStats_.empty();
    if (noData) ImGui::BeginDisabled();

    // awlui::Button renders the label verbatim, so scope the ID with
    // PushID instead of a "##" suffix
    ImGui::PushID(instanceId_);
    bool reportClicked = awlui::Button(buttonLabel, awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm);
    if (reportClicked) {
        // WoW chat drops everything past the first line on paste and
        // caps a message around 255 characters, so pack the summary
        // into a single short line: view, duration, top 5 entries.
        uint32_t totalSeconds = cachedDuration_ms_ / 1000;
        char header[64];
        std::snprintf(header, sizeof(header), " (%u:%02u): ",
                      totalSeconds / 60, totalSeconds % 60);
        std::string line = std::string(getMeterViewName(config_.view_type)) + header;

        size_t reportCount = std::min<size_t>(5, cachedCombatStats_.size());
        for (size_t i = 0; i < reportCount; ++i) {
            const auto& stats = cachedCombatStats_[i];

            std::string name = stats.actor_guid;
            auto nameIt = guidToName.find(std::string_view(stats.actor_guid));
            if (nameIt != guidToName.end()) {
                name = std::string(nameIt->second);
            } else if (combatGuidToName) {
                auto combatNameIt = combatGuidToName->find(stats.actor_guid);
                if (combatNameIt != combatGuidToName->end()) {
                    name = combatNameIt->second;
                }
            }
            // Drop the realm suffix ("Name-Realm" -> "Name")
            if (size_t dash = name.find('-'); dash != std::string::npos) {
                name.resize(dash);
            }

            char percentBuf[16];
            std::snprintf(percentBuf, sizeof(percentBuf), "%.1f", stats.percent_of_total);

            if (i > 0) line += ", ";
            line += std::to_string(i + 1) + ". " + name + " "
                  + ui::format::formatAmount(stats.effective_amount)
                  + " (" + percentBuf + "%)";
        }

        ImGui::SetClipboardText(line.c_str());
        reportFeedbackTimer_ = 2.0f;
    }

    if (noData) {
        ImGui::EndDisabled();
    } else if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("meter.report_tooltip"));
    }
    ImGui::PopID();
}
