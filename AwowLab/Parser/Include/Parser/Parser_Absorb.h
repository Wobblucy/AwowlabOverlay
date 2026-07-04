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
// SPELL_ABSORBED Parser
// Token count: 22-23
// Structure varies based on whether the absorb was from a spell or swing:
//
// SPELL absorb (22 tokens):
// base(11) + absorbed_spell(3) + absorber(4) + absorb_spell(3) + amount(1)
//
// SWING absorb (19 tokens):
// base(11) + absorber(4) + absorb_spell(3) + amount(1)
//
// absorber(4) = absorber_guid, absorber_name, absorber_flags, absorber_raid_flags
// =============================================================================

struct SpellAbsorbedTag {};

template <>
struct EventParser<SpellAbsorbedTag> {
    static constexpr size_t minimum_token_count() { return 19; }

    static AbsorbedEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        AbsorbedEventData data{};
        PARSER_VALIDATE("SPELL_ABSORBED", tokens, minimum_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Determine if this is a SPELL absorb or SWING absorb
        // SPELL absorb has the absorbed spell info at indices 11-13
        // SWING absorb goes directly to absorber info at index 11

        bool is_spell_absorb = (tokens.size() >= 22);

        size_t absorber_start;
        if (is_spell_absorb) {
            // Parse the spell that was absorbed
            util::parseSpellInfo(tokens, data.spell);
            absorber_start = 14;
        } else {
            // SWING absorb - no spell prefix
            data.spell.spell_id = 0;
            data.spell.spell_name = "Melee";
            data.spell.spell_school = SpellSchool::Physical;
            absorber_start = 11;
        }

        // Parse absorber info (4 tokens)
        data.absorber_guid = tokens[absorber_start];
        data.absorber_name = tokens[absorber_start + 1];
        data.absorber_flags = UnitFlags::parse(tokens[absorber_start + 2]);
        data.absorber_flags.raidMarker = UnitFlags::parseRaidMarker(tokens[absorber_start + 3]);

        // Parse absorb spell info (3 tokens after absorber)
        size_t absorb_spell_start = absorber_start + 4;
        data.absorb_spell_id = util::parseUint32(tokens[absorb_spell_start]);
        data.absorb_spell_name = tokens[absorb_spell_start + 1];
        data.absorb_spell_school = util::parseUint32(tokens[absorb_spell_start + 2]);

        // Amount absorbed (last token or second to last)
        data.amount = util::parseInt64(tokens[absorb_spell_start + 3]);

        // Critical flag may be present
        if (tokens.size() > absorb_spell_start + 4) {
            data.critical = util::parseBool(tokens[absorb_spell_start + 4]);
        }

        data.has_position = false;

        PARSER_FINALIZE("SPELL_ABSORBED", tokens);
        PARSER_DEBUG("SPELL_ABSORBED",
            "absorbed_spell=" << data.spell.spell_id
            << " absorber=" << data.absorber_name
            << " absorb_spell=" << data.absorb_spell_id << " (" << data.absorb_spell_name << ")"
            << " amount=" << data.amount);

        return data;
    }
};

// =============================================================================
// SPELL_ABSORBED_SUPPORT Parser
// Token count: 23-24
// Same as SPELL_ABSORBED but with supporterGUID at the end
// =============================================================================

struct SpellAbsorbedSupportTag {};

template <>
struct EventParser<SpellAbsorbedSupportTag> {
    static constexpr size_t minimum_token_count() { return 20; }

    static AbsorbedEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        AbsorbedEventData data{};
        PARSER_VALIDATE("SPELL_ABSORBED_SUPPORT", tokens, minimum_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Similar structure to SPELL_ABSORBED
        bool is_spell_absorb = (tokens.size() >= 23);

        size_t absorber_start;
        if (is_spell_absorb) {
            util::parseSpellInfo(tokens, data.spell);
            absorber_start = 14;
        } else {
            data.spell.spell_id = 0;
            data.spell.spell_name = "Melee";
            data.spell.spell_school = SpellSchool::Physical;
            absorber_start = 11;
        }

        // Parse absorber info
        data.absorber_guid = tokens[absorber_start];
        data.absorber_name = tokens[absorber_start + 1];
        data.absorber_flags = UnitFlags::parse(tokens[absorber_start + 2]);
        data.absorber_flags.raidMarker = UnitFlags::parseRaidMarker(tokens[absorber_start + 3]);

        // Parse absorb spell info
        size_t absorb_spell_start = absorber_start + 4;
        data.absorb_spell_id = util::parseUint32(tokens[absorb_spell_start]);
        data.absorb_spell_name = tokens[absorb_spell_start + 1];
        data.absorb_spell_school = util::parseUint32(tokens[absorb_spell_start + 2]);

        // Amount absorbed
        data.amount = util::parseInt64(tokens[absorb_spell_start + 3]);

        // Supporter GUID is at the end
        data.supporter_guid = tokens[tokens.size() - 1];

        data.has_position = false;

        PARSER_FINALIZE("SPELL_ABSORBED_SUPPORT", tokens);
        PARSER_DEBUG("SPELL_ABSORBED_SUPPORT",
            "absorbed_spell=" << data.spell.spell_id
            << " absorb_spell=" << data.absorb_spell_id
            << " amount=" << data.amount
            << " supporter=" << data.supporter_guid);

        return data;
    }
};

} // namespace parser
