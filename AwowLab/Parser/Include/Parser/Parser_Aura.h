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
// Aura Events - No Position Data
// These events track buff/debuff application, removal, refresh, and stacks
// Token count: 14-15 (no advanced combat logging data)
// =============================================================================

// =============================================================================
// SPELL_AURA_APPLIED Parser
// Token count: 14 or 15
// Structure: base(11) + spell(3) + auraType(1) + [amount]
// Token 14 (index 13) is optional - present for absorb shields
// =============================================================================

struct SpellAuraAppliedTag {};

template <>
struct EventParser<SpellAuraAppliedTag> {
    static constexpr size_t expected_token_count() { return 15; }

    static AuraEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        AuraEventData data{};
        PARSER_VALIDATE("SPELL_AURA_APPLIED", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Aura type at index 14 (after spell prefix)
        data.aura_type = (tokens[14] == "BUFF") ? AuraType::BUFF : AuraType::DEBUFF;

        // No position data for aura events
        data.has_position = false;

        PARSER_FINALIZE("SPELL_AURA_APPLIED", tokens);
        PARSER_DEBUG("SPELL_AURA_APPLIED",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")"
            << " type=" << (data.aura_type == AuraType::BUFF ? "BUFF" : "DEBUFF")
            << " amount=" << data.amount);

        return data;
    }
};

// =============================================================================
// SPELL_AURA_REMOVED Parser
// Token count: 15
// Structure: base(11) + spell(3) + auraType(1)
// =============================================================================

struct SpellAuraRemovedTag {};

template <>
struct EventParser<SpellAuraRemovedTag> {
    static constexpr size_t expected_token_count() { return 15; }

    static AuraEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        AuraEventData data{};
        PARSER_VALIDATE("SPELL_AURA_REMOVED", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Aura type at index 14 (after spell prefix)
        data.aura_type = (tokens[14] == "BUFF") ? AuraType::BUFF : AuraType::DEBUFF;

        // No position data
        data.has_position = false;

        PARSER_FINALIZE("SPELL_AURA_REMOVED", tokens);
        PARSER_DEBUG("SPELL_AURA_REMOVED",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")"
            << " type=" << (data.aura_type == AuraType::BUFF ? "BUFF" : "DEBUFF"));

        return data;
    }
};

// =============================================================================
// SPELL_AURA_REFRESH Parser
// Token count: 15
// Structure: base(11) + spell(3) + auraType(1)
// =============================================================================

struct SpellAuraRefreshTag {};

template <>
struct EventParser<SpellAuraRefreshTag> {
    static constexpr size_t expected_token_count() { return 15; }

    static AuraEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        AuraEventData data{};
        PARSER_VALIDATE("SPELL_AURA_REFRESH", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Aura type at index 14 (after spell prefix)
        data.aura_type = (tokens[14] == "BUFF") ? AuraType::BUFF : AuraType::DEBUFF;

        // No position data
        data.has_position = false;

        PARSER_FINALIZE("SPELL_AURA_REFRESH", tokens);
        PARSER_DEBUG("SPELL_AURA_REFRESH",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")"
            << " type=" << (data.aura_type == AuraType::BUFF ? "BUFF" : "DEBUFF"));

        return data;
    }
};

// =============================================================================
// SPELL_AURA_APPLIED_DOSE Parser
// Token count: 15
// Structure: base(11) + spell(3) + auraType(1) + stacks(1)
// =============================================================================

struct SpellAuraAppliedDoseTag {};

template <>
struct EventParser<SpellAuraAppliedDoseTag> {
    static constexpr size_t expected_token_count() { return 16; }

    static AuraEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        AuraEventData data{};
        PARSER_VALIDATE("SPELL_AURA_APPLIED_DOSE", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Aura type at index 14
        data.aura_type = (tokens[14] == "BUFF") ? AuraType::BUFF : AuraType::DEBUFF;

        // Stack count at index 15
        data.amount = util::parseInt32(tokens[15]);

        // No position data
        data.has_position = false;

        PARSER_FINALIZE("SPELL_AURA_APPLIED_DOSE", tokens);
        PARSER_DEBUG("SPELL_AURA_APPLIED_DOSE",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")"
            << " type=" << (data.aura_type == AuraType::BUFF ? "BUFF" : "DEBUFF")
            << " stacks=" << data.amount);

        return data;
    }
};

// =============================================================================
// SPELL_AURA_REMOVED_DOSE Parser
// Token count: 15
// Structure: base(11) + spell(3) + auraType(1) + stacks(1)
// =============================================================================

struct SpellAuraRemovedDoseTag {};

template <>
struct EventParser<SpellAuraRemovedDoseTag> {
    static constexpr size_t expected_token_count() { return 16; }

    static AuraEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        AuraEventData data{};
        PARSER_VALIDATE("SPELL_AURA_REMOVED_DOSE", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Aura type at index 14
        data.aura_type = (tokens[14] == "BUFF") ? AuraType::BUFF : AuraType::DEBUFF;

        // Stack count at index 15
        data.amount = util::parseInt32(tokens[15]);

        // No position data
        data.has_position = false;

        PARSER_FINALIZE("SPELL_AURA_REMOVED_DOSE", tokens);
        PARSER_DEBUG("SPELL_AURA_REMOVED_DOSE",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")"
            << " type=" << (data.aura_type == AuraType::BUFF ? "BUFF" : "DEBUFF")
            << " stacks=" << data.amount);

        return data;
    }
};

// =============================================================================
// SPELL_AURA_BROKEN Parser
// Token count: 14
// Structure: base(11) + spell(3) + auraType(1)
// Triggered when an aura is broken (e.g., crowd control broken by damage)
// =============================================================================

struct SpellAuraBrokenTag {};

template <>
struct EventParser<SpellAuraBrokenTag> {
    static constexpr size_t expected_token_count() { return 15; }

    static AuraEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        AuraEventData data{};
        PARSER_VALIDATE("SPELL_AURA_BROKEN", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Aura type at index 14
        data.aura_type = (tokens[14] == "BUFF") ? AuraType::BUFF : AuraType::DEBUFF;

        // No position data
        data.has_position = false;

        PARSER_FINALIZE("SPELL_AURA_BROKEN", tokens);
        PARSER_DEBUG("SPELL_AURA_BROKEN",
            "spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")");

        return data;
    }
};

// =============================================================================
// SPELL_AURA_BROKEN_SPELL Parser
// Token count: 18
// Structure: base(11) + spell(3) + extraSpell(3) + auraType(1)
// Triggered when an aura is broken by a specific spell
// =============================================================================

struct SpellAuraBrokenSpellTag {};

template <>
struct EventParser<SpellAuraBrokenSpellTag> {
    static constexpr size_t expected_token_count() { return 18; }

    static AuraEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        AuraEventData data{};
        PARSER_VALIDATE("SPELL_AURA_BROKEN_SPELL", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info (the aura that was broken)
        util::parseSpellInfo(tokens, data.spell);

        // Extra spell info (the spell that broke it) at indices 14-16
        util::parseExtraSpellInfo(tokens, 14, data.extra_spell_id, data.extra_spell_name, data.extra_spell_school);

        // Aura type at index 17
        data.aura_type = (tokens[17] == "BUFF") ? AuraType::BUFF : AuraType::DEBUFF;

        // No position data
        data.has_position = false;

        PARSER_FINALIZE("SPELL_AURA_BROKEN_SPELL", tokens);
        PARSER_DEBUG("SPELL_AURA_BROKEN_SPELL",
            "aura=" << data.spell.spell_id << " broken by spell=" << data.extra_spell_id
            << " (" << data.extra_spell_name << ")");

        return data;
    }
};

} // namespace parser
