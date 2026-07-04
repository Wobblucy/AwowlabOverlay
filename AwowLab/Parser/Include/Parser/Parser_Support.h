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
// Support Event Data Structure
// Support events have an extra supporterGUID at the end identifying the
// augmentation evoker or other support source
// =============================================================================

struct SupportEventData : public DamageEventData {
    std::string_view supporter_guid;  // GUID of the support source (e.g., Aug Evoker)
};

struct HealSupportEventData : public HealEventData {
    std::string_view supporter_guid;
};

// =============================================================================
// SPELL_DAMAGE_SUPPORT Parser
// Token count: 44
// Structure: base(11) + spell(3) + advanced(19) + damage_suffix(10) + supporterGUID(1)
// Note: Support events have 10 damage suffix fields (no isOffHand), not 11
// Position indices: 28(x), 29(y), 30(map), 31(facing), 32(level)
// =============================================================================

struct SpellDamageSupportTag {};

template <>
struct EventParser<SpellDamageSupportTag> {
    static constexpr size_t expected_token_count() { return 44; }

    static SupportEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        SupportEventData data{};
        PARSER_VALIDATE("SPELL_DAMAGE_SUPPORT", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Parse advanced info (includes position)
        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        // Damage suffix starts at index 33 (after advanced params which end at 32)
        // Suffix: amount, baseAmount, overkill, school, resisted, blocked, absorbed, critical, glancing, crushing
        // Note: Support events have 10 damage suffix fields (no isOffHand), not 11
        // position 0 = mitigated (post-armor/absorb) amount
        // position 1 = baseAmount (unmitigated, before any mitigation)
        constexpr size_t DAMAGE_SUFFIX_START = 33;
        data.mitigated_amount = util::parseInt64(tokens[DAMAGE_SUFFIX_START]);      // post-mitigation
        data.amount = util::parseInt64(tokens[DAMAGE_SUFFIX_START + 1]);            // baseAmount (unmitigated)
        data.overkill = util::parseInt64(tokens[DAMAGE_SUFFIX_START + 2]);
        data.damage_school = util::parseUint32(tokens[DAMAGE_SUFFIX_START + 3]);
        data.resisted = util::parseInt64(tokens[DAMAGE_SUFFIX_START + 4]);
        data.blocked = util::parseInt64(tokens[DAMAGE_SUFFIX_START + 5]);
        data.absorbed = util::parseInt64(tokens[DAMAGE_SUFFIX_START + 6]);
        data.critical = util::parseBool(tokens[DAMAGE_SUFFIX_START + 7]);
        data.glancing = util::parseBool(tokens[DAMAGE_SUFFIX_START + 8]);
        data.crushing = util::parseBool(tokens[DAMAGE_SUFFIX_START + 9]);
        data.is_off_hand = false;  // Support events don't have isOffHand field

        // Supporter GUID is at the end (token 43)
        data.supporter_guid = tokens[tokens.size() - 1];

        PARSER_FINALIZE("SPELL_DAMAGE_SUPPORT", tokens);
        PARSER_DEBUG("SPELL_DAMAGE_SUPPORT",
            "spell=" << data.spell.spell_id << " amount=" << data.amount
            << " supporter=" << data.supporter_guid
            << " pos=(" << data.advanced_info.position.x_loc << ", "
            << data.advanced_info.position.y_loc << ")");

        return data;
    }

    static UnitRendering parse_position(const std::vector<std::string_view>& tokens) {
        UnitRendering ur{};
        auto data = parse_and_return(tokens);
        if (data.advanced_info.position.valid) {
            ur.timestamp_ms = data.timestamp_ms;
            ur.x_loc = data.advanced_info.position.x_loc;
            ur.y_loc = data.advanced_info.position.y_loc;
            ur.map_id = data.advanced_info.position.map_id;
            ur.radians = data.advanced_info.position.facing;
        }
        return ur;
    }
};

// =============================================================================
// SPELL_PERIODIC_DAMAGE_SUPPORT Parser
// Token count: 44 (same structure as SPELL_DAMAGE_SUPPORT)
// =============================================================================

struct SpellPeriodicDamageSupportTag {};

template <>
struct EventParser<SpellPeriodicDamageSupportTag> : public EventParser<SpellDamageSupportTag> {
    static constexpr size_t expected_token_count() { return 44; }

    static SupportEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<SpellDamageSupportTag>::parse_and_return(tokens);
        PARSER_FINALIZE("SPELL_PERIODIC_DAMAGE_SUPPORT", tokens);
        return data;
    }
};

// =============================================================================
// SPELL_HEAL_SUPPORT Parser
// Token count: 39
// Structure: base(11) + spell(3) + advanced(19) + heal_suffix(5) + supporterGUID(1)
// heal_suffix: amount, baseAmount, overhealing, absorbed, critical
// Position indices: 28(x), 29(y), 30(map), 31(facing), 32(level)
// =============================================================================

struct SpellHealSupportTag {};

template <>
struct EventParser<SpellHealSupportTag> {
    static constexpr size_t expected_token_count() { return 39; }

    static HealSupportEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        HealSupportEventData data{};
        PARSER_VALIDATE("SPELL_HEAL_SUPPORT", tokens, expected_token_count(), data);

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
        data.overhealing = util::parseInt64(tokens[HEAL_SUFFIX_START + 2]);  // overhealing
        data.absorbed = util::parseInt64(tokens[HEAL_SUFFIX_START + 3]);     // absorbed
        data.critical = util::parseBool(tokens[HEAL_SUFFIX_START + 4]);      // critical

        // Supporter GUID is at the end
        data.supporter_guid = tokens[tokens.size() - 1];

        PARSER_FINALIZE("SPELL_HEAL_SUPPORT", tokens);
        PARSER_DEBUG("SPELL_HEAL_SUPPORT",
            "spell=" << data.spell.spell_id << " amount=" << data.amount
            << " supporter=" << data.supporter_guid
            << " pos=(" << data.advanced_info.position.x_loc / 100.0f << ", "
            << data.advanced_info.position.y_loc / 100.0f << ")");

        return data;
    }

    static UnitRendering parse_position(const std::vector<std::string_view>& tokens) {
        UnitRendering ur{};
        auto data = parse_and_return(tokens);
        if (data.advanced_info.position.valid) {
            ur.timestamp_ms = data.timestamp_ms;
            ur.x_loc = data.advanced_info.position.x_loc;
            ur.y_loc = data.advanced_info.position.y_loc;
            ur.map_id = data.advanced_info.position.map_id;
            ur.radians = data.advanced_info.position.facing;
        }
        return ur;
    }
};

// =============================================================================
// SPELL_PERIODIC_HEAL_SUPPORT Parser
// Token count: 39 (same structure as SPELL_HEAL_SUPPORT)
// =============================================================================

struct SpellPeriodicHealSupportTag {};

template <>
struct EventParser<SpellPeriodicHealSupportTag> : public EventParser<SpellHealSupportTag> {
    static HealSupportEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<SpellHealSupportTag>::parse_and_return(tokens);
        PARSER_FINALIZE("SPELL_PERIODIC_HEAL_SUPPORT", tokens);
        return data;
    }
};

// =============================================================================
// SWING_DAMAGE_LANDED_SUPPORT Parser
// Token count: 41 (input), aligned to 44
// Structure: base(11) + [synthetic spell(3)] + advanced(19) + damage_suffix(10) + supporterGUID(1)
// Note: SWING events have 10 damage suffix fields (no isOffHand), not 11 like SPELL
// Inserts synthetic spell tokens (1, "Melee", 0x1) to align with SPELL_DAMAGE format
// =============================================================================

struct SwingDamageLandedSupportTag {};

template <>
struct EventParser<SwingDamageLandedSupportTag> {
    // SWING_DAMAGE_LANDED_SUPPORT is unusual: unlike plain
    // SWING_DAMAGE_LANDED (which has no spell prefix), the SUPPORT
    // variant ships WITH a spell prefix carrying the supporter's aura
    // (e.g. "395152, Ebon Might, 0xc"). Structure is therefore the
    // same as SPELL_DAMAGE_SUPPORT:
    //   base(11) + spell(3) + advanced(19) + damage_suffix(10) + supporterGUID(1) = 44
    // The previous version of this parser injected a synthetic
    // "1, Melee, 1" spell prefix on top of the REAL prefix, which
    // shifted every subsequent index by 3 and made the mitigated /
    // base amounts read random advanced-info fields. Aug's melee
    // support contribution then aggregated under spell_id=1 ("Spell 0"
    // in the meter) with junk numbers - the smoking-gun bug behind the
    // "Spell 0" row showing 17M Effective with only 11K Raw.
    static constexpr size_t expected_token_count() { return 44; }

    static SupportEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        SupportEventData data{};
        PARSER_VALIDATE("SWING_DAMAGE_LANDED_SUPPORT", tokens, expected_token_count(), data);

        util::parseBaseEvent(tokens, data);
        util::parseSpellInfo(tokens, data.spell);
        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        constexpr size_t DAMAGE_SUFFIX_START = 33;
        data.mitigated_amount = util::parseInt64(tokens[DAMAGE_SUFFIX_START]);      // post-mitigation
        data.amount = util::parseInt64(tokens[DAMAGE_SUFFIX_START + 1]);            // base
        data.overkill = util::parseInt64(tokens[DAMAGE_SUFFIX_START + 2]);
        data.damage_school = util::parseUint32(tokens[DAMAGE_SUFFIX_START + 3]);
        data.resisted = util::parseInt64(tokens[DAMAGE_SUFFIX_START + 4]);
        data.blocked = util::parseInt64(tokens[DAMAGE_SUFFIX_START + 5]);
        data.absorbed = util::parseInt64(tokens[DAMAGE_SUFFIX_START + 6]);
        data.critical = util::parseBool(tokens[DAMAGE_SUFFIX_START + 7]);
        data.glancing = util::parseBool(tokens[DAMAGE_SUFFIX_START + 8]);
        data.crushing = util::parseBool(tokens[DAMAGE_SUFFIX_START + 9]);
        data.is_off_hand = false;  // SWING support events don't have isOffHand

        // Supporter GUID is the trailing token
        data.supporter_guid = tokens[tokens.size() - 1];

        PARSER_FINALIZE("SWING_DAMAGE_LANDED_SUPPORT", tokens);
        PARSER_DEBUG("SWING_DAMAGE_LANDED_SUPPORT",
            "spell=" << data.spell.spell_id << " amount=" << data.amount
            << " supporter=" << data.supporter_guid);

        return data;
    }

    static UnitRendering parse_position(const std::vector<std::string_view>& tokens) {
        UnitRendering ur{};
        auto data = parse_and_return(tokens);
        if (data.advanced_info.position.valid) {
            ur.timestamp_ms = data.timestamp_ms;
            ur.x_loc = data.advanced_info.position.x_loc;
            ur.y_loc = data.advanced_info.position.y_loc;
            ur.map_id = data.advanced_info.position.map_id;
            ur.radians = data.advanced_info.position.facing;
        }
        return ur;
    }
};

} // namespace parser
