#pragma once
#include <vector>
#include <string_view>
#include <charconv>
#include "../structures.h"
#include "Parser.h"
#include "ParserDebug.h"
#include "ParserMacros.h"
#include "Parser_Damage.h"  // For util namespace

namespace parser {

// =============================================================================
// Helper function to parse MissType from string
// =============================================================================

namespace util {
    inline MissType parseMissType(std::string_view sv) {
        if (sv == "ABSORB") return MissType::ABSORB;
        if (sv == "BLOCK") return MissType::BLOCK;
        if (sv == "DEFLECT") return MissType::DEFLECT;
        if (sv == "DODGE") return MissType::DODGE;
        if (sv == "EVADE") return MissType::EVADE;
        if (sv == "IMMUNE") return MissType::IMMUNE;
        if (sv == "MISS") return MissType::MISS;
        if (sv == "PARRY") return MissType::PARRY;
        if (sv == "REFLECT") return MissType::REFLECT;
        if (sv == "RESIST") return MissType::RESIST;
        return MissType::MISS;  // Default
    }
}

// =============================================================================
// SPELL_MISSED Parser
// Token count: 17 (simple miss) or 20 (ABSORB/BLOCK/RESIST with amounts)
// - 17 tokens: base(11) + spell(3) + missType(1) + nil(1) + ST(1)
// - 20 tokens: base(11) + spell(3) + missType(ABSORB) + nil + amountMissed + totalPending + nil + AOE
// No position data
// =============================================================================

struct SpellMissedTag {};

template <>
struct EventParser<SpellMissedTag> {
    static constexpr size_t expected_token_count() { return 17; }
    static constexpr size_t expected_token_count_absorb() { return 20; }

    static MissedEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        MissedEventData data{};

        // Check if this is an ABSORB/BLOCK/RESIST type (has extra tokens)
        bool is_absorb_type = false;
        if (tokens.size() > 14) {
            std::string_view missType = tokens[14];
            is_absorb_type = (missType == "ABSORB" || missType == "BLOCK" || missType == "RESIST");
        }

        size_t required = is_absorb_type ? expected_token_count_absorb() : expected_token_count();
        PARSER_VALIDATE("SPELL_MISSED", tokens, required, data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Miss type at index 14
        data.miss_type = util::parseMissType(tokens[14]);

        // isOffHand at index 15 (may be "nil")
        data.is_off_hand = util::parseBool(tokens[15]);

        if (is_absorb_type) {
            // amountMissed at index 16
            data.amount_missed = util::parseInt64(tokens[16]);
            // totalPending at index 17
            data.total_pending = util::parseInt64(tokens[17]);
            // critical at index 18 (may be "nil")
            data.critical = util::parseBool(tokens[18]);
        }

        // No position data for missed events
        data.has_position = false;

        PARSER_FINALIZE("SPELL_MISSED", tokens);
        PARSER_DEBUG("SPELL_MISSED",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")"
            << " missType=" << static_cast<int>(data.miss_type)
            << " amount=" << data.amount_missed);

        return data;
    }
};

// =============================================================================
// SPELL_PERIODIC_MISSED Parser
// Token count: same as SPELL_MISSED
// =============================================================================

struct SpellPeriodicMissedTag {};

template <>
struct EventParser<SpellPeriodicMissedTag> : public EventParser<SpellMissedTag> {
    static MissedEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<SpellMissedTag>::parse_and_return(tokens);
        PARSER_FINALIZE("SPELL_PERIODIC_MISSED", tokens);
        return data;
    }
};

// =============================================================================
// SWING_MISSED Parser
// Token count: 13-15
// Structure: base(11) + missType(1) + isOffHand(1) + [amountMissed] + [critical]
// No spell prefix, no position data
// =============================================================================

struct SwingMissedTag {};

template <>
struct EventParser<SwingMissedTag> {
    static constexpr size_t expected_token_count() { return 13; }

    static MissedEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        MissedEventData data{};
        PARSER_VALIDATE("SWING_MISSED", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // No spell prefix for SWING events
        data.spell.spell_id = 0;
        data.spell.spell_name = "Melee";
        data.spell.spell_school = SpellSchool::Physical;

        // Miss type at index 11 (no spell prefix offset)
        data.miss_type = util::parseMissType(tokens[11]);

        // isOffHand at index 12 (may be "nil")
        if (tokens.size() > 12) {
            data.is_off_hand = util::parseBool(tokens[12]);
        }

        // amountMissed at index 13 (for ABSORB, BLOCK, RESIST)
        if (tokens.size() > 13) {
            data.amount_missed = util::parseInt64(tokens[13]);
        }

        // critical at index 14 if present
        if (tokens.size() > 14) {
            data.critical = util::parseBool(tokens[14]);
        }

        // No position data
        data.has_position = false;

        PARSER_FINALIZE("SWING_MISSED", tokens);
        PARSER_DEBUG("SWING_MISSED",
            "missType=" << static_cast<int>(data.miss_type)
            << " amount=" << data.amount_missed
            << " offhand=" << (data.is_off_hand ? "yes" : "no"));

        return data;
    }
};

// =============================================================================
// RANGE_MISSED Parser
// Token count: same as SPELL_MISSED
// Structure: base(11) + spell(3) + missType + ...
// =============================================================================

struct RangeMissedTag {};

template <>
struct EventParser<RangeMissedTag> : public EventParser<SpellMissedTag> {
    static MissedEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<SpellMissedTag>::parse_and_return(tokens);
        PARSER_FINALIZE("RANGE_MISSED", tokens);
        return data;
    }
};

} // namespace parser
