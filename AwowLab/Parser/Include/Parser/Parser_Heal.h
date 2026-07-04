#pragma once
#include <vector>
#include <string_view>
#include <charconv>
#include <cmath>
#include "../structures.h"
#include "Parser.h"
#include "ParserDebug.h"
#include "ParserMacros.h"
#include "Parser_Damage.h"  // For util namespace

namespace parser {

// =============================================================================
// SPELL_HEAL Parser
// Token count: 38
// Structure: base(11) + spell(3) + advanced(19) + heal_suffix(5)
// Position indices: 28(x), 29(y), 30(map), 31(facing), 32(level)
// Heal suffix (TWW 11.0+): amount, baseAmount, overhealing, absorbed, critical
// - amount: effective heal attempted
// - baseAmount: raw heal before modifiers (we skip this)
// - overhealing: wasted healing (target at full HP)
// - absorbed: healing absorbed by debuffs
// - critical: 1 if crit, 0/nil otherwise
// =============================================================================

struct SpellHealTag {};

template <>
struct EventParser<SpellHealTag> {
    static constexpr size_t expected_token_count() { return 38; }

    static HealEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        HealEventData data{};
        PARSER_VALIDATE("SPELL_HEAL", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Parse advanced info (includes position)
        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        // Heal suffix starts at index 33 (after advanced params which end at 32)
        // TWW 11.0+ format: amount, baseAmount, overhealing, absorbed, critical
        constexpr size_t HEAL_SUFFIX_START = 33;
        data.amount = util::parseInt64(tokens[HEAL_SUFFIX_START]);      // effective heal attempted
        // Skip baseAmount at tokens[HEAL_SUFFIX_START + 1]
        data.overhealing = util::parseInt64(tokens[HEAL_SUFFIX_START + 2]);  // overhealing (was +1)
        data.absorbed = util::parseInt64(tokens[HEAL_SUFFIX_START + 3]);     // absorbed (was +2)
        data.critical = util::parseBool(tokens[HEAL_SUFFIX_START + 4]);      // critical (was +3)

        PARSER_FINALIZE("SPELL_HEAL", tokens);
        PARSER_DEBUG("SPELL_HEAL",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")"
            << " amount=" << data.amount
            << " overheal=" << data.overhealing
            << " crit=" << (data.critical ? "yes" : "no"));

        return data;
    }
};

// =============================================================================
// SPELL_PERIODIC_HEAL Parser
// Token count: 36 (same structure as SPELL_HEAL)
// =============================================================================

struct SpellPeriodicHealTag {};

template <>
struct EventParser<SpellPeriodicHealTag> : public EventParser<SpellHealTag> {
    static HealEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<SpellHealTag>::parse_and_return(tokens);
        PARSER_FINALIZE("SPELL_PERIODIC_HEAL", tokens);
        return data;
    }
};

} // namespace parser
