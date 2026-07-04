#include "UI/Panels/Settings/UIPhaseEditorPanel.h"
#include "UI/AwlUI/Widgets.h"
#include "Core/PhaseSettings.h"
#include "Core/UnifiedSettings.h"
#include "Core/LocalizationManager.h"
#include <imgui.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

namespace {

// Format milliseconds as "m:ss" for rule descriptors and emote rows
std::string formatTime(int32_t ms) {
    if (ms < 0) ms = 0;
    int32_t totalSec = ms / 1000;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", totalSec / 60, totalSec % 60);
    return buf;
}

// Parse the add-split input: "m:ss" or a plain seconds count.
// Returns the elapsed milliseconds, or -1 when the text doesn't parse.
int32_t parseTimeInputMs(const char* text) {
    const char* colon = std::strchr(text, ':');
    char* end = nullptr;
    if (colon) {
        long minutes = std::strtol(text, &end, 10);
        if (end != colon || minutes < 0) return -1;
        long seconds = std::strtol(colon + 1, &end, 10);
        if (end == colon + 1 || *end != '\0' || seconds < 0 || seconds >= 60) return -1;
        return static_cast<int32_t>((minutes * 60 + seconds) * 1000);
    }
    long seconds = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || seconds < 0) return -1;
    return static_cast<int32_t>(seconds * 1000);
}

// Cap display text at maxBytes without splitting a UTF-8 sequence
std::string truncateForDisplay(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return text.substr(0, cut) + "...";
}

}  // namespace

UIPhaseEditorPanel::UIPhaseEditorPanel() = default;
UIPhaseEditorPanel::~UIPhaseEditorPanel() = default;

void UIPhaseEditorPanel::render(const PhaseEditorData& data) {
    if (!visible_) return;

    // The overlay's meter fills a borderless fullscreen window, so a fresh
    // editor can open behind it. Pull it to the front and focus it the
    // first frame it shows so it's never lost under the meter.
    if (justOpened_) {
        ImGui::SetNextWindowFocus();
        justOpened_ = false;
    }

    // Size to content and drop the collapse triangle: the window has no
    // fixed body, so it should just fit its rules/emotes rather than open
    // at a padded fixed size the user then has to resize.
    if (ImGui::Begin(L("phases.title"), &visible_,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        // A plain Close button in the body - the title-bar X is small and
        // easy to miss against the game behind a click-through overlay.
        if (awlui::Button(L("btn.close"), awlui::ButtonVariant::Secondary,
                          awlui::ButtonSize::Sm)) {
            visible_ = false;
        }
        ImGui::Spacing();

        renderRuleList(data);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        renderAddTimeSplit(data.encounterId);

        // Boss casts (overlay live capture); hidden when the owner
        // supplies none
        if (!data.casts.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            renderCastList(data);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        renderEmoteList(data);

        lastMeasuredSize_ = ImGui::GetWindowSize();
    }
    ImGui::End();
}

void UIPhaseEditorPanel::renderRuleList(const PhaseEditorData& data) {
    auto& settings = PhaseSettings::instance();
    const auto* current = settings.rulesFor(data.encounterId);
    if (!current) {
        ImGui::TextDisabled("%s", L("phases.no_rules"));
        return;
    }

    // Work on a copy so removing a rule mid-loop can't invalidate the
    // list we're iterating
    std::vector<PhaseSettings::PhaseRule> rules = *current;
    int32_t pullStart = data.ruleInputs.pullStart_ms;

    for (size_t i = 0; i < rules.size(); ++i) {
        const auto& rule = rules[i];
        ImGui::PushID(static_cast<int>(i));

        if (awlui::IconButton("remove_rule", "x")) {
            settings.removeRule(data.encounterId, rule);
            saveAndFlagChanged();
        }
        ImGui::SameLine();

        // Kind + descriptor, e.g. "First cast of Devour" / "At 2:30"
        const char* kindText = L("phases.kind_cast");
        std::string descriptor;
        std::string fullText;  // Tooltip when the descriptor got truncated
        switch (rule.kind) {
            case PhaseSettings::RuleKind::SpellCast: {
                kindText = L("phases.kind_cast");
                if (data.spellNames) {
                    auto it = data.spellNames->find(rule.spellId);
                    if (it != data.spellNames->end()) descriptor = it->second;
                }
                if (descriptor.empty()) descriptor = std::to_string(rule.spellId);
                break;
            }
            case PhaseSettings::RuleKind::Elapsed:
                kindText = L("phases.kind_time");
                descriptor = formatTime(rule.elapsedMs);
                break;
            case PhaseSettings::RuleKind::Emote:
                kindText = L("phases.kind_emote");
                descriptor = truncateForDisplay(rule.emoteText, 60);
                if (descriptor != rule.emoteText) fullText = rule.emoteText;
                break;
        }
        ImGui::Text("%s %s", kindText, descriptor.c_str());
        if (!fullText.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", fullText.c_str());
        }
        if (!rule.label.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", rule.label.c_str());
        }

        // Where the rule landed this pull, if it fired at all
        std::optional<int32_t> resolved = phase::resolveRuleTime(rule, data.ruleInputs);
        ImGui::SameLine();
        if (resolved) {
            ImGui::TextDisabled("-> %s", formatTime(*resolved - pullStart).c_str());
        } else {
            ImGui::TextDisabled("%s", L("phases.not_this_pull"));
        }

        ImGui::PopID();
    }
}

void UIPhaseEditorPanel::renderAddTimeSplit(uint32_t encounterId) {
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputTextWithHint("##PhaseTimeInput", L("phases.time_hint"),
                             timeInput_, sizeof(timeInput_));
    ImGui::SameLine();

    if (awlui::Button(L("phases.add_time"), awlui::ButtonVariant::Secondary, awlui::ButtonSize::Sm)) {
        int32_t elapsedMs = parseTimeInputMs(timeInput_);
        if (elapsedMs > 0) {
            PhaseSettings::PhaseRule rule;
            rule.kind = PhaseSettings::RuleKind::Elapsed;
            rule.elapsedMs = elapsedMs;
            PhaseSettings::instance().addRule(encounterId, rule);
            saveAndFlagChanged();
            timeInput_[0] = '\0';
        }
    }
}

void UIPhaseEditorPanel::renderCastList(const PhaseEditorData& data) {
    ImGui::Text("%s", L("phases.casts_header"));

    auto& settings = PhaseSettings::instance();
    int32_t pullStart = data.ruleInputs.pullStart_ms;

    for (size_t i = 0; i < data.casts.size(); ++i) {
        const auto& cast = data.casts[i];
        ImGui::PushID(static_cast<int>(i + 10000));

        bool alreadyRule = settings.hasRule(data.encounterId, cast.spellId);
        if (alreadyRule) ImGui::BeginDisabled();
        if (awlui::Button(L("phases.split_here"), awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
            settings.addRule(data.encounterId, cast.spellId);
            saveAndFlagChanged();
        }
        if (alreadyRule) ImGui::EndDisabled();
        ImGui::SameLine();

        std::string name = !cast.spell_name.empty() ? cast.spell_name
                                                    : std::to_string(cast.spellId);
        ImGui::Text("[%s] %s", formatTime(cast.time_ms - pullStart).c_str(), name.c_str());

        ImGui::PopID();
    }
}

void UIPhaseEditorPanel::renderEmoteList(const PhaseEditorData& data) {
    ImGui::Text("%s", L("phases.emotes_header"));

    // Fixed-width, capped-height scroll region. With the window set to
    // auto-resize a (0,0) child would collapse to nothing, so give it a
    // concrete size and let the list scroll inside it.
    ImGui::BeginChild("PhaseEmoteList", ImVec2(520.0f, 160.0f), true);

    if (data.emotes.empty()) {
        ImGui::TextDisabled("%s", L("phases.no_emotes"));
        ImGui::EndChild();
        return;
    }

    auto& settings = PhaseSettings::instance();

    // The same emote repeats across the fight; show each distinct text
    // once at the time it first fired (the list is already time-sorted)
    std::vector<const PhaseEditorData::EmoteRow*> distinct;
    for (const auto& emote : data.emotes) {
        bool seen = false;
        for (const auto* prior : distinct) {
            if (prior->text == emote.text) {
                seen = true;
                break;
            }
        }
        if (!seen) distinct.push_back(&emote);
    }

    int32_t pullStart = data.ruleInputs.pullStart_ms;
    for (size_t i = 0; i < distinct.size(); ++i) {
        const auto& emote = *distinct[i];
        ImGui::PushID(static_cast<int>(i));

        if (awlui::Button(L("phases.split_here"), awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
            PhaseSettings::PhaseRule rule;
            rule.kind = PhaseSettings::RuleKind::Emote;
            rule.emoteText = emote.text;
            settings.addRule(data.encounterId, rule);
            saveAndFlagChanged();
        }
        ImGui::SameLine();

        std::string shown = truncateForDisplay(emote.text, 60);
        ImGui::Text("[%s] %s: %s", formatTime(emote.time_ms - pullStart).c_str(),
                    emote.source_name.c_str(), shown.c_str());
        if (shown != emote.text && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", emote.text.c_str());
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
}

void UIPhaseEditorPanel::saveAndFlagChanged() {
    auto& cache = SettingsCache::instance();
    cache.get().phaseSettingsJson = PhaseSettings::instance().toJson();
    cache.markDirty();
    rulesChanged_ = true;
}
