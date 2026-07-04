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
// UNIT_DIED Parser
// Token count: 12
// Structure: base(11) + recapID(1)
// Triggered when a unit dies
// =============================================================================

struct UnitDiedTag {};

template <>
struct EventParser<UnitDiedTag> {
    static constexpr size_t expected_token_count() { return 12; }

    static DeathEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DeathEventData data{};

        if (tokens.size() < expected_token_count()) {
#ifndef NDEBUG
            debug::recordParseFailure("UNIT_DIED", tokens.size() > 3 ? tokens[3] : "",
                                      tokens.size(), tokens);
#endif
            return data;
        }

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Recap ID at index 11 (for Death Recap feature)
        data.recap_id = util::parseInt32(tokens[11]);

        data.unconscious_on_death = false;
        data.has_position = false;

#ifndef NDEBUG
        debug::recordTokenCount("UNIT_DIED", tokens.size());
        PARSER_DEBUG("UNIT_DIED",
            "unit=" << data.dest_name << " (" << data.dest_guid << ")"
            << " recapID=" << data.recap_id);
#endif

        return data;
    }
};

// =============================================================================
// UNIT_DESTROYED Parser
// Token count: 12
// Structure: base(11) + recapID(1)
// Triggered when a unit is destroyed (totems, objects, etc.)
// =============================================================================

struct UnitDestroyedTag {};

template <>
struct EventParser<UnitDestroyedTag> : public EventParser<UnitDiedTag> {
    static DeathEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<UnitDiedTag>::parse_and_return(tokens);
#ifndef NDEBUG
        debug::recordTokenCount("UNIT_DESTROYED", tokens.size());
#endif
        return data;
    }
};

// =============================================================================
// UNIT_DISSIPATES Parser
// Token count: 12
// Structure: base(11) + recapID(1)
// Triggered when a unit dissipates (fades away)
// =============================================================================

struct UnitDissipatesTag {};

template <>
struct EventParser<UnitDissipatesTag> : public EventParser<UnitDiedTag> {
    static DeathEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<UnitDiedTag>::parse_and_return(tokens);
#ifndef NDEBUG
        debug::recordTokenCount("UNIT_DISSIPATES", tokens.size());
#endif
        return data;
    }
};

// =============================================================================
// PARTY_KILL Parser
// Token count: 12
// Structure: base(11) + recapID(1)
// Triggered when a party member kills something
// =============================================================================

struct PartyKillTag {};

template <>
struct EventParser<PartyKillTag> {
    static constexpr size_t expected_token_count() { return 12; }

    static DeathEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DeathEventData data{};

        if (tokens.size() < expected_token_count()) {
#ifndef NDEBUG
            debug::recordParseFailure("PARTY_KILL", tokens.size() > 3 ? tokens[3] : "",
                                      tokens.size(), tokens);
#endif
            return data;
        }

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Recap ID at index 11
        data.recap_id = util::parseInt32(tokens[11]);

        data.has_position = false;

#ifndef NDEBUG
        debug::recordTokenCount("PARTY_KILL", tokens.size());
        PARSER_DEBUG("PARTY_KILL",
            "killer=" << data.source_name << " killed=" << data.dest_name
            << " recapID=" << data.recap_id);
#endif

        return data;
    }
};

// =============================================================================
// SPELL_SUMMON Parser
// Token count: 14
// Structure: base(11) + spell(3)
// Triggered when a unit summons something (pets, totems, etc.)
// =============================================================================

struct SpellSummonTag {};

template <>
struct EventParser<SpellSummonTag> {
    static constexpr size_t expected_token_count() { return 14; }

    static SummonEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        SummonEventData data{};

        if (tokens.size() < expected_token_count()) {
#ifndef NDEBUG
            debug::recordParseFailure("SPELL_SUMMON", tokens.size() > 3 ? tokens[3] : "",
                                      tokens.size(), tokens);
#endif
            return data;
        }

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info (the summon spell)
        util::parseSpellInfo(tokens, data.spell);

        data.has_position = false;

#ifndef NDEBUG
        debug::recordTokenCount("SPELL_SUMMON", tokens.size());
        PARSER_DEBUG("SPELL_SUMMON",
            "summoner=" << data.source_name << " summoned=" << data.dest_name
            << " spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")");
#endif

        return data;
    }
};

// =============================================================================
// SPELL_CREATE Parser
// Token count: 14
// Structure: base(11) + spell(3)
// Triggered when a spell creates something (conjured items, etc.)
// =============================================================================

struct SpellCreateTag {};

template <>
struct EventParser<SpellCreateTag> : public EventParser<SpellSummonTag> {
    static SummonEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<SpellSummonTag>::parse_and_return(tokens);
#ifndef NDEBUG
        debug::recordTokenCount("SPELL_CREATE", tokens.size());
#endif
        return data;
    }
};

// =============================================================================
// SPELL_RESURRECT Parser
// Token count: 14
// Structure: base(11) + spell(3)
// Triggered when a unit is resurrected
// =============================================================================

struct SpellResurrectTag {};

template <>
struct EventParser<SpellResurrectTag> {
    static constexpr size_t expected_token_count() { return 14; }

    static SummonEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        SummonEventData data{};

        if (tokens.size() < expected_token_count()) {
#ifndef NDEBUG
            debug::recordParseFailure("SPELL_RESURRECT", tokens.size() > 3 ? tokens[3] : "",
                                      tokens.size(), tokens);
#endif
            return data;
        }

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info (the resurrect spell)
        util::parseSpellInfo(tokens, data.spell);

        data.has_position = false;

#ifndef NDEBUG
        debug::recordTokenCount("SPELL_RESURRECT", tokens.size());
        PARSER_DEBUG("SPELL_RESURRECT",
            "resurrector=" << data.source_name << " resurrected=" << data.dest_name
            << " spell=" << data.spell.spell_id << " (" << data.spell.spell_name << ")");
#endif

        return data;
    }
};

// =============================================================================
// SPELL_INSTAKILL Parser
// Token count: 14
// Structure: base(11) + spell(3)
// Triggered when a spell instantly kills a unit
// =============================================================================

struct SpellInstakillTag {};

template <>
struct EventParser<SpellInstakillTag> {
    static constexpr size_t expected_token_count() { return 14; }

    static DeathEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DeathEventData data{};

        if (tokens.size() < expected_token_count()) {
#ifndef NDEBUG
            debug::recordParseFailure("SPELL_INSTAKILL", tokens.size() > 3 ? tokens[3] : "",
                                      tokens.size(), tokens);
#endif
            return data;
        }

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info (the instakill spell)
        data.spell_id = util::parseUint32(tokens[TokenIndex::SpellID]);
        data.spell_name = tokens[TokenIndex::SpellName];

        data.has_position = false;

#ifndef NDEBUG
        debug::recordTokenCount("SPELL_INSTAKILL", tokens.size());
        PARSER_DEBUG("SPELL_INSTAKILL",
            "spell=" << data.spell_id << " (" << data.spell_name << ")"
            << " killed=" << data.dest_name);
#endif

        return data;
    }
};

// =============================================================================
// ENVIRONMENTAL_DAMAGE Parser
// Token count: varies
// Structure: base(11) + environmentalType(1) + amount + ...
// Triggered by environmental damage (fire, falling, drowning, etc.)
// =============================================================================

struct EnvironmentalDamageTag {};

template <>
struct EventParser<EnvironmentalDamageTag> {
    static constexpr size_t minimum_token_count() { return 13; }

    static DamageEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DamageEventData data{};

        if (tokens.size() < minimum_token_count()) {
#ifndef NDEBUG
            debug::recordParseFailure("ENVIRONMENTAL_DAMAGE", tokens.size() > 3 ? tokens[3] : "",
                                      tokens.size(), tokens);
#endif
            return data;
        }

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Environmental type at index 11
        std::string_view envType = tokens[11];
        data.spell.spell_id = 0;
        data.spell.spell_name = envType;  // Use env type as "spell name"

        // Map environmental type to school
        if (envType == "Fire" || envType == "Lava") {
            data.spell.spell_school = SpellSchool::Fire;
        } else if (envType == "Drowning" || envType == "Fatigue") {
            data.spell.spell_school = SpellSchool::Physical;
        } else if (envType == "Slime") {
            data.spell.spell_school = SpellSchool::Nature;
        } else if (envType == "Falling") {
            data.spell.spell_school = SpellSchool::Physical;
        } else {
            data.spell.spell_school = SpellSchool::Physical;
        }

        // Amount at index 12
        data.amount = util::parseInt64(tokens[12]);

        // Additional damage fields may follow
        if (tokens.size() > 13) {
            data.overkill = util::parseInt64(tokens[13]);
        }
        if (tokens.size() > 14) {
            data.damage_school = util::parseUint32(tokens[14]);
        }
        if (tokens.size() > 15) {
            data.resisted = util::parseInt64(tokens[15]);
        }
        if (tokens.size() > 16) {
            data.blocked = util::parseInt64(tokens[16]);
        }
        if (tokens.size() > 17) {
            data.absorbed = util::parseInt64(tokens[17]);
        }
        if (tokens.size() > 18) {
            data.critical = util::parseBool(tokens[18]);
        }

        data.advanced_info.position.valid = false;

#ifndef NDEBUG
        debug::recordTokenCount("ENVIRONMENTAL_DAMAGE", tokens.size());
        PARSER_DEBUG("ENVIRONMENTAL_DAMAGE",
            "type=" << envType << " amount=" << data.amount
            << " target=" << data.dest_name);
#endif

        return data;
    }
};

// =============================================================================
// EMOTE Parser
// Token count: 11 (just base event)
// Structure: base(11)
// Triggered for combat-related emotes
// =============================================================================

struct EmoteTag {};

template <>
struct EventParser<EmoteTag> {
    static constexpr size_t expected_token_count() { return 11; }

    static BaseEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        BaseEventData data{};

        if (tokens.size() < expected_token_count()) {
#ifndef NDEBUG
            debug::recordParseFailure("EMOTE", tokens.size() > 3 ? tokens[3] : "",
                                      tokens.size(), tokens);
#endif
            return data;
        }

        // Parse base event
        util::parseBaseEvent(tokens, data);

#ifndef NDEBUG
        debug::recordTokenCount("EMOTE", tokens.size());
        PARSER_DEBUG("EMOTE",
            "source=" << data.source_name << " dest=" << data.dest_name);
#endif

        return data;
    }
};

} // namespace parser
