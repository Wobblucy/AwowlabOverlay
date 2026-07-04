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
// SPELL_CAST_SUCCESS Parser
// Token count: 33
// Structure: base(11) + spell(3) + advanced(19)
// Position indices: 28(x), 29(y), 30(map), 31(facing), 32(level)
// Reference: Warcraft wiki COMBAT_LOG_EVENT page
// =============================================================================

struct SpellCastSuccessTag {};

template <>
struct EventParser<SpellCastSuccessTag> {
    static constexpr size_t expected_token_count() { return 33; }

    static CastEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        CastEventData data{};
        PARSER_VALIDATE("SPELL_CAST_SUCCESS", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Parse advanced info (includes position)
        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        PARSER_FINALIZE("SPELL_CAST_SUCCESS", tokens);
        PARSER_DEBUG("SPELL_CAST_SUCCESS",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")"
            << " pos=(" << data.advanced_info.position.x_loc << ", "
            << data.advanced_info.position.y_loc << ")");

        return data;
    }
};

// =============================================================================
// SPELL_CAST_START Parser
// Token count: 14
// Structure: base(11) + spell(3)
// No advanced data (no position)
// =============================================================================

struct SpellCastStartTag {};

template <>
struct EventParser<SpellCastStartTag> {
    static constexpr size_t expected_token_count() { return 14; }

    static CastEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        CastEventData data{};
        PARSER_VALIDATE("SPELL_CAST_START", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // No advanced info for SPELL_CAST_START
        data.advanced_info.position.valid = false;

        PARSER_FINALIZE("SPELL_CAST_START", tokens);
        PARSER_DEBUG("SPELL_CAST_START",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")");

        return data;
    }
};

// =============================================================================
// SPELL_CAST_FAILED Parser
// Token count: 15
// Structure: base(11) + spell(3) + fail_reason(1)
// No advanced data (no position)
// =============================================================================

struct SpellCastFailedTag {};

template <>
struct EventParser<SpellCastFailedTag> {
    static constexpr size_t expected_token_count() { return 15; }

    static CastEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        CastEventData data{};
        PARSER_VALIDATE("SPELL_CAST_FAILED", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Fail reason at index 13 (after spell prefix)
        data.fail_reason = tokens[13];

        // No advanced info
        data.advanced_info.position.valid = false;

        PARSER_FINALIZE("SPELL_CAST_FAILED", tokens);
        PARSER_DEBUG("SPELL_CAST_FAILED",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")"
            << " reason=\"" << data.fail_reason << "\"");

        return data;
    }
};

// =============================================================================
// SPELL_EMPOWER_START Parser
// Token count: 14 (same as SPELL_CAST_START)
// =============================================================================

struct SpellEmpowerStartTag {};

template <>
struct EventParser<SpellEmpowerStartTag> {
    static constexpr size_t expected_token_count() { return 14; }

    static CastEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        CastEventData data{};
        PARSER_VALIDATE("SPELL_EMPOWER_START", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);
        util::parseSpellInfo(tokens, data.spell);
        data.advanced_info.position.valid = false;

        PARSER_FINALIZE("SPELL_EMPOWER_START", tokens);
        PARSER_DEBUG("SPELL_EMPOWER_START",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")");

        return data;
    }
};

// =============================================================================
// SPELL_EMPOWER_END Parser
// Token count: 15
// =============================================================================

struct SpellEmpowerEndTag {};

template <>
struct EventParser<SpellEmpowerEndTag> {
    static constexpr size_t expected_token_count() { return 15; }

    static CastEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        CastEventData data{};
        PARSER_VALIDATE("SPELL_EMPOWER_END", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);
        util::parseSpellInfo(tokens, data.spell);
        data.advanced_info.position.valid = false;

        PARSER_FINALIZE("SPELL_EMPOWER_END", tokens);
        PARSER_DEBUG("SPELL_EMPOWER_END",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")");

        return data;
    }
};

// =============================================================================
// SPELL_EMPOWER_INTERRUPT Parser
// Token count: 15
// =============================================================================

struct SpellEmpowerInterruptTag {};

template <>
struct EventParser<SpellEmpowerInterruptTag> {
    static constexpr size_t expected_token_count() { return 15; }

    static CastEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        CastEventData data{};
        PARSER_VALIDATE("SPELL_EMPOWER_INTERRUPT", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);
        util::parseSpellInfo(tokens, data.spell);
        data.advanced_info.position.valid = false;

        PARSER_FINALIZE("SPELL_EMPOWER_INTERRUPT", tokens);
        PARSER_DEBUG("SPELL_EMPOWER_INTERRUPT",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")");

        return data;
    }
};

} // namespace parser
