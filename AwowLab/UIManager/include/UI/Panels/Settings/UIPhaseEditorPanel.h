#pragma once

#include <imgui.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/PhaseResolver.h"

// Everything the phase editor needs, as plain data. Both apps fill
// this from their own sources: the main app from its encounter
// databases, the overlay from the live session snapshot. The rules
// themselves live in PhaseSettings and are read/edited in place.
struct PhaseEditorData {
    uint32_t encounterId = 0;

    // Trigger lookups + pull window for the "-> 2:30 / not reached"
    // display next to each rule
    phase::RuleInputs ruleInputs;

    // Emote lines of this pull, time-sorted; the editor lists each
    // distinct text once so a split can be dropped on it
    struct EmoteRow {
        int32_t time_ms = 0;
        std::string source_name;
        std::string text;
    };
    std::vector<EmoteRow> emotes;

    // First casts of this pull worth splitting on (hostile casters),
    // time-sorted. Optional: the overlay fills this from live capture;
    // the main app leaves it empty (cast rules come from the spell
    // context menu there) and the section stays hidden.
    struct CastRow {
        uint32_t spellId = 0;
        int32_t time_ms = 0;
        std::string spell_name;
    };
    std::vector<CastRow> casts;

    // Spell names for cast-rule descriptors (nullptr = show the id)
    const std::unordered_map<uint32_t, std::string>* spellNames = nullptr;
};

// Settings panel for boss phase rules
// Lists the split rules saved for the loaded encounter with the time
// each one resolved to this pull, lets the user add a fixed-time split,
// and lists the pull's emotes (and, when provided, boss casts) so a
// split can be dropped on one of them.
class UIPhaseEditorPanel {
public:
    UIPhaseEditorPanel();
    ~UIPhaseEditorPanel();

    // Render the panel (no-op while hidden)
    void render(const PhaseEditorData& data);

    // Visibility control
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    void toggleVisible() { visible_ = !visible_; }

    // True once after any rule edit since the last call; the owner
    // polls this to rebuild the meter phase windows
    bool consumeRulesChanged() {
        bool changed = rulesChanged_;
        rulesChanged_ = false;
        return changed;
    }

    // Size ImGui laid out on the last rendered frame; (0,0) until the
    // panel has rendered once. The overlay's auto-grow uses this so
    // the panel isn't clipped by the small at-a-glance window.
    ImVec2 getLastMeasuredSize() const { return lastMeasuredSize_; }

private:
    bool visible_ = false;
    bool rulesChanged_ = false;
    ImVec2 lastMeasuredSize_ = ImVec2(0, 0);

    // "m:ss" text for the add-time-split input
    char timeInput_[16] = {};

    // Persist the current rules to the settings cache and raise the
    // changed flag for the owner
    void saveAndFlagChanged();

    void renderRuleList(const PhaseEditorData& data);
    void renderAddTimeSplit(uint32_t encounterId);
    void renderCastList(const PhaseEditorData& data);
    void renderEmoteList(const PhaseEditorData& data);
};
