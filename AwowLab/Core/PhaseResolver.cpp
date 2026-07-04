#include "PhaseResolver.h"

#include <algorithm>

namespace phase {

std::optional<int32_t> resolveRuleTime(const PhaseSettings::PhaseRule& rule,
                                       const RuleInputs& inputs) {
    switch (rule.kind) {
        case PhaseSettings::RuleKind::SpellCast:
            if (inputs.firstCastTime) {
                return inputs.firstCastTime(rule.spellId);
            }
            return std::nullopt;
        case PhaseSettings::RuleKind::Elapsed: {
            int32_t time_ms = inputs.pullStart_ms + rule.elapsedMs;
            if (time_ms < inputs.pullEnd_ms) {
                return time_ms;
            }
            return std::nullopt;
        }
        case PhaseSettings::RuleKind::Emote:
            if (inputs.firstEmoteTime) {
                return inputs.firstEmoteTime(rule.emoteText);
            }
            return std::nullopt;
    }
    return std::nullopt;
}

std::vector<ResolvedPhase> resolvePhases(const std::vector<PhaseSettings::PhaseRule>& rules,
                                         const RuleInputs& inputs) {
    // Each rule contributes one boundary; rules whose trigger never
    // fired this pull are dropped.
    struct PhaseBoundary {
        int32_t time_ms;
        const std::string* label;
    };
    std::vector<PhaseBoundary> boundaries;
    boundaries.reserve(rules.size());
    for (const auto& rule : rules) {
        if (auto time = resolveRuleTime(rule, inputs)) {
            boundaries.push_back({*time, &rule.label});
        }
    }
    if (boundaries.empty()) {
        return {};
    }

    std::sort(boundaries.begin(), boundaries.end(),
              [](const PhaseBoundary& a, const PhaseBoundary& b) {
                  return a.time_ms < b.time_ms;
              });

    // Two rules landing on the same millisecond would produce an empty
    // phase window - keep only the first
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end(),
                                 [](const PhaseBoundary& a, const PhaseBoundary& b) {
                                     return a.time_ms == b.time_ms;
                                 }),
                     boundaries.end());

    // P1 always runs from the pull start to the first boundary; each
    // boundary opens the next phase. Unnamed rules get positional
    // "P2"/"P3" labels.
    std::vector<ResolvedPhase> phases;
    phases.reserve(boundaries.size() + 1);
    phases.push_back(ResolvedPhase{"P1", inputs.pullStart_ms, boundaries.front().time_ms});
    for (size_t i = 0; i < boundaries.size(); ++i) {
        int32_t end_ms = (i + 1 < boundaries.size()) ? boundaries[i + 1].time_ms
                                                     : inputs.pullEnd_ms;
        std::string label = boundaries[i].label->empty() ? ("P" + std::to_string(i + 2))
                                                         : *boundaries[i].label;
        phases.push_back(ResolvedPhase{std::move(label), boundaries[i].time_ms, end_ms});
    }
    return phases;
}

} // namespace phase
