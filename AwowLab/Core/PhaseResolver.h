#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "PhaseSettings.h"

// Turns an encounter's phase rules into concrete phase windows for one
// pull. The rules live in PhaseSettings; what changes per pull is when
// (and whether) each trigger fired, which the caller supplies through
// small lookup callbacks. Shared by the main app's meter rebuild and
// the live overlay so both resolve rules with the same logic.
namespace phase {

// One resolved phase window. Times are on whatever clock the inputs
// use (log-relative for live pulls, segment-relative for re-parsed
// historical segments).
struct ResolvedPhase {
    std::string label;   // "P1", "P2" or the rule's own label
    int32_t start_ms = 0;
    int32_t end_ms = 0;
};

// Trigger lookups plus the pull window. P1 opens at pullStart, the
// last phase closes at pullEnd, and an elapsed rule only fires when
// its mark lands before pullEnd. Null callbacks mean "that trigger
// kind never fires this pull" (e.g. no emote data available).
struct RuleInputs {
    int32_t pullStart_ms = 0;
    int32_t pullEnd_ms = 0;
    std::function<std::optional<int32_t>(uint32_t spellId)> firstCastTime;
    std::function<std::optional<int32_t>(std::string_view emoteText)> firstEmoteTime;
};

// Time a single rule resolved to this pull, or nullopt when its
// trigger never fired (spell not cast, emote not seen, pull ended
// before the elapsed mark). The phase editor shows this next to each
// rule.
std::optional<int32_t> resolveRuleTime(const PhaseSettings::PhaseRule& rule,
                                       const RuleInputs& inputs);

// Full resolution: one boundary per fired rule, sorted by time, with
// same-millisecond duplicates dropped (they would produce an empty
// phase window), then folded into phase windows - P1 runs from the
// pull start to the first boundary and each boundary opens the next
// phase. Unnamed rules get positional "P2"/"P3" labels. Returns empty
// when no rule fired.
std::vector<ResolvedPhase> resolvePhases(const std::vector<PhaseSettings::PhaseRule>& rules,
                                         const RuleInputs& inputs);

} // namespace phase
