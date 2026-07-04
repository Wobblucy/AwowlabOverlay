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
// SPELL_DISPEL Parser
// Token count: 17-18
// Structure: base(11) + spell(3) + extraSpell(3) + auraType(1)
// The spell(3) is the dispel spell, extraSpell(3) is the spell that was dispelled
// No position data
// =============================================================================

struct SpellDispelTag {};

template <>
struct EventParser<SpellDispelTag> {
    static constexpr size_t expected_token_count() { return 18; }

    static DispelEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DispelEventData data{};

        if (tokens.size() < expected_token_count()) {
#ifndef NDEBUG
            debug::recordParseFailure("SPELL_DISPEL", tokens.size() > 3 ? tokens[3] : "",
                                      tokens.size(), tokens);
#endif
            return data;
        }

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse dispel spell info (the spell that did the dispelling)
        util::parseSpellInfo(tokens, data.spell);

        // Extra spell info (the spell that was dispelled) at indices 14-16
        util::parseExtraSpellInfo(tokens, 14, data.extra_spell_id, data.extra_spell_name, data.extra_spell_school);

        // Aura type at index 17
        data.aura_type = (tokens[17] == "BUFF") ? AuraType::BUFF : AuraType::DEBUFF;

        // No position data
        data.has_position = false;

#ifndef NDEBUG
        debug::recordTokenCount("SPELL_DISPEL", tokens.size());
        PARSER_DEBUG("SPELL_DISPEL",
            "dispeller=" << data.spell.spell_id << " (" << data.spell.spell_name << ")"
            << " dispelled=" << data.extra_spell_id << " (" << data.extra_spell_name << ")"
            << " type=" << (data.aura_type == AuraType::BUFF ? "BUFF" : "DEBUFF"));
#endif

        return data;
    }
};

// =============================================================================
// SPELL_DISPEL_FAILED Parser
// Token count: 14
// Structure: base(11) + spell(3)
// When a dispel attempt fails
// =============================================================================

struct SpellDispelFailedTag {};

template <>
struct EventParser<SpellDispelFailedTag> {
    static constexpr size_t expected_token_count() { return 14; }

    static DispelEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DispelEventData data{};

        if (tokens.size() < expected_token_count()) {
#ifndef NDEBUG
            debug::recordParseFailure("SPELL_DISPEL_FAILED", tokens.size() > 3 ? tokens[3] : "",
                                      tokens.size(), tokens);
#endif
            return data;
        }

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info (the dispel spell that failed)
        util::parseSpellInfo(tokens, data.spell);

        // No extra spell or aura type - just the failed dispel attempt
        data.extra_spell_id = 0;
        data.has_position = false;

#ifndef NDEBUG
        debug::recordTokenCount("SPELL_DISPEL_FAILED", tokens.size());
        PARSER_DEBUG("SPELL_DISPEL_FAILED",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")");
#endif

        return data;
    }
};

// =============================================================================
// SPELL_STOLEN Parser
// Token count: 18
// Structure: base(11) + spell(3) + extraSpell(3) + auraType(1)
// When a buff is stolen (e.g., Spellsteal)
// =============================================================================

struct SpellStolenTag {};

template <>
struct EventParser<SpellStolenTag> {
    static constexpr size_t expected_token_count() { return 18; }

    static DispelEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DispelEventData data{};

        if (tokens.size() < expected_token_count()) {
#ifndef NDEBUG
            debug::recordParseFailure("SPELL_STOLEN", tokens.size() > 3 ? tokens[3] : "",
                                      tokens.size(), tokens);
#endif
            return data;
        }

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse stealing spell info
        util::parseSpellInfo(tokens, data.spell);

        // Extra spell info (the buff that was stolen) at indices 14-16
        util::parseExtraSpellInfo(tokens, 14, data.extra_spell_id, data.extra_spell_name, data.extra_spell_school);

        // Aura type at index 17
        data.aura_type = (tokens[17] == "BUFF") ? AuraType::BUFF : AuraType::DEBUFF;

        data.has_position = false;

#ifndef NDEBUG
        debug::recordTokenCount("SPELL_STOLEN", tokens.size());
        PARSER_DEBUG("SPELL_STOLEN",
            "stealer=" << data.spell.spell_id << " stolen=" << data.extra_spell_id
            << " (" << data.extra_spell_name << ")");
#endif

        return data;
    }
};

// =============================================================================
// SPELL_INTERRUPT Parser
// Token count: 17
// Structure: base(11) + spell(3) + extraSpell(3)
// The spell(3) is the interrupt, extraSpell(3) is the spell that was interrupted
// =============================================================================

struct SpellInterruptTag {};

template <>
struct EventParser<SpellInterruptTag> {
    static constexpr size_t expected_token_count() { return 17; }

    static DispelEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DispelEventData data{};

        if (tokens.size() < expected_token_count()) {
#ifndef NDEBUG
            debug::recordParseFailure("SPELL_INTERRUPT", tokens.size() > 3 ? tokens[3] : "",
                                      tokens.size(), tokens);
#endif
            return data;
        }

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse interrupt spell info
        util::parseSpellInfo(tokens, data.spell);

        // Extra spell info (the spell that was interrupted) at indices 14-16
        util::parseExtraSpellInfo(tokens, 14, data.extra_spell_id, data.extra_spell_name, data.extra_spell_school);

        data.has_position = false;

#ifndef NDEBUG
        debug::recordTokenCount("SPELL_INTERRUPT", tokens.size());
        PARSER_DEBUG("SPELL_INTERRUPT",
            "interrupter=" << data.spell.spell_id << " (" << data.spell.spell_name << ")"
            << " interrupted=" << data.extra_spell_id << " (" << data.extra_spell_name << ")");
#endif

        return data;
    }
};

// =============================================================================
// SPELL_EXTRA_ATTACKS Parser
// Token count: 15
// Structure: base(11) + spell(3) + amount(1)
// Triggered by abilities that grant extra attacks (e.g., Windfury)
// =============================================================================

struct SpellExtraAttacksTag {};

template <>
struct EventParser<SpellExtraAttacksTag> {
    static constexpr size_t expected_token_count() { return 15; }

    static DispelEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DispelEventData data{};

        if (tokens.size() < expected_token_count()) {
#ifndef NDEBUG
            debug::recordParseFailure("SPELL_EXTRA_ATTACKS", tokens.size() > 3 ? tokens[3] : "",
                                      tokens.size(), tokens);
#endif
            return data;
        }

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Amount of extra attacks at index 14
        data.amount = util::parseInt64(tokens[14]);

        data.has_position = false;

#ifndef NDEBUG
        debug::recordTokenCount("SPELL_EXTRA_ATTACKS", tokens.size());
        PARSER_DEBUG("SPELL_EXTRA_ATTACKS",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")"
            << " extra_attacks=" << data.amount);
#endif

        return data;
    }
};

} // namespace parser
