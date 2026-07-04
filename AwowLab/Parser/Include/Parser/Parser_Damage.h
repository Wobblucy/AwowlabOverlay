#pragma once
#include <vector>
#include <string_view>
#include <charconv>
#include <cmath>
#include <sstream>
#include "../structures.h"
#include "Parser.h"
#include "ParserDebug.h"
#include "ParserMacros.h"
#include "../../../Core/ErrorLogger.h"

namespace parser {

// =============================================================================
// Common Parsing Utilities
// =============================================================================

namespace util {

    // Parse int64 from string_view (handles negative and large values)
    inline int64_t parseInt64(std::string_view sv) {
        if (sv.empty() || sv == "nil") return 0;
        int64_t value = 0;
        std::from_chars(sv.data(), sv.data() + sv.size(), value);
        return value;
    }

    // Parse uint64 from string_view
    inline uint64_t parseUint64(std::string_view sv) {
        if (sv.empty() || sv == "nil") return 0;
        uint64_t value = 0;
        std::from_chars(sv.data(), sv.data() + sv.size(), value);
        return value;
    }

    // Parse uint32 from string_view
    inline uint32_t parseUint32(std::string_view sv) {
        if (sv.empty() || sv == "nil") return 0;
        uint32_t value = 0;
        std::from_chars(sv.data(), sv.data() + sv.size(), value);
        return value;
    }

    // Parse int32 from string_view
    inline int32_t parseInt32(std::string_view sv) {
        if (sv.empty() || sv == "nil") return 0;
        int32_t value = 0;
        std::from_chars(sv.data(), sv.data() + sv.size(), value);
        return value;
    }

    // Parse float from string_view
    inline float parseFloat(std::string_view sv) {
        if (sv.empty() || sv == "nil") return 0.0f;
        float value = 0.0f;
        std::from_chars(sv.data(), sv.data() + sv.size(), value);
        return value;
    }

    // Parse bool from string_view (handles "1", "nil", etc.)
    inline bool parseBool(std::string_view sv) {
        return sv == "1";
    }

    // Parse base event data common to all events
    // Note: timestamp_ms is int32_t which allows negative values for pre-encounter events
    inline void parseBaseEvent(const std::vector<std::string_view>& tokens, BaseEventData& data) {
        uint64_t absolute_ts = EventParser<void>::parse_timestamp_ms(tokens);
        if (get_log_start_timestamp_ms() == 0) {
            get_log_start_timestamp_ms() = absolute_ts;
        }
        // Calculate relative timestamp - can be negative for events before log start
        // This is intentional: negative timestamps represent pre-encounter events
        int64_t relative_ts = static_cast<int64_t>(absolute_ts) - static_cast<int64_t>(get_log_start_timestamp_ms());
        data.timestamp_ms = static_cast<int32_t>(relative_ts);

        data.source_guid = tokens[TokenIndex::SourceGUID];
        data.source_name = tokens[TokenIndex::SourceName];
        data.source_flags = UnitFlags::parse(tokens[TokenIndex::SourceFlags]);
        data.source_flags.raidMarker = UnitFlags::parseRaidMarker(tokens[TokenIndex::SourceRaidFlags]);

        data.dest_guid = tokens[TokenIndex::DestGUID];
        data.dest_name = tokens[TokenIndex::DestName];
        data.dest_flags = UnitFlags::parse(tokens[TokenIndex::DestFlags]);
        data.dest_flags.raidMarker = UnitFlags::parseRaidMarker(tokens[TokenIndex::DestRaidFlags]);
    }

    // Parse spell prefix data (tokens 11-13)
    inline void parseSpellInfo(const std::vector<std::string_view>& tokens, SpellInfo& spell) {
        spell.spell_id = parseUint32(tokens[TokenIndex::SpellID]);
        spell.spell_name = tokens[TokenIndex::SpellName];
        spell.spell_school = parseUint32(tokens[TokenIndex::SpellSchool]);
    }

    // Parse advanced unit info for SPELL events (tokens 14-32)
    inline void parseAdvancedSpellInfo(const std::vector<std::string_view>& tokens, AdvancedUnitInfo& info) {
        using namespace TokenIndex::AdvancedSpell;

        info.info_guid = tokens[InfoGUID];
        info.owner_guid = tokens[OwnerGUID];
        info.current_hp = parseUint64(tokens[CurrentHP]);
        info.max_hp = parseUint64(tokens[MaxHP]);
        info.attack_power = parseUint32(tokens[AttackPower]);
        info.spell_power = parseUint32(tokens[SpellPower]);
        info.armor = parseUint32(tokens[Armor]);
        info.absorb = parseUint32(tokens[Absorb]);
        // Skip Unknown1 (22) and Unknown2 (23) - extra fields not in wiki
        info.power_type = static_cast<int8_t>(parseInt32(tokens[PowerTypeIdx]));
        info.current_power = parseUint32(tokens[CurrentPower]);
        info.max_power = parseUint32(tokens[MaxPower]);
        info.power_cost = parseUint32(tokens[PowerCost]);

        // Position data (raw world coordinates) - indices 28-32
        info.position.x_loc = parseFloat(tokens[PosX]);
        info.position.y_loc = parseFloat(tokens[PosY]);
        info.position.map_id = static_cast<uint16_t>(parseUint32(tokens[MapID]));
        float facing = parseFloat(tokens[Facing]);
        info.position.facing = static_cast<uint16_t>(std::lround(facing * 10000.0f));
        info.level = static_cast<uint8_t>(parseUint32(tokens[Level]));
        info.position.valid = true;
    }

    // Parse advanced unit info with token offset (for SWING events that lack spell prefix)
    // offset = 3 for SWING events (no spell prefix means indices shifted by 3)
    inline void parseAdvancedSpellInfoWithOffset(const std::vector<std::string_view>& tokens,
                                                  AdvancedUnitInfo& info,
                                                  size_t offset) {
        using namespace TokenIndex::AdvancedSpell;

        info.info_guid = tokens[InfoGUID - offset];
        info.owner_guid = tokens[OwnerGUID - offset];
        info.current_hp = parseUint64(tokens[CurrentHP - offset]);
        info.max_hp = parseUint64(tokens[MaxHP - offset]);
        info.attack_power = parseUint32(tokens[AttackPower - offset]);
        info.spell_power = parseUint32(tokens[SpellPower - offset]);
        info.armor = parseUint32(tokens[Armor - offset]);
        info.absorb = parseUint32(tokens[Absorb - offset]);
        info.power_type = static_cast<int8_t>(parseInt32(tokens[PowerTypeIdx - offset]));
        info.current_power = parseUint32(tokens[CurrentPower - offset]);
        info.max_power = parseUint32(tokens[MaxPower - offset]);
        info.power_cost = parseUint32(tokens[PowerCost - offset]);

        info.position.x_loc = parseFloat(tokens[PosX - offset]);
        info.position.y_loc = parseFloat(tokens[PosY - offset]);
        info.position.map_id = static_cast<uint16_t>(parseUint32(tokens[MapID - offset]));
        float facing = parseFloat(tokens[Facing - offset]);
        info.position.facing = static_cast<uint16_t>(std::lround(facing * 10000.0f));
        info.level = static_cast<uint8_t>(parseUint32(tokens[Level - offset]));
        info.position.valid = true;
    }

    // Parse extra spell info (for dispels, interrupts, aura breaks, etc.)
    // Parses spell_id, spell_name, spell_school from consecutive tokens starting at index
    inline void parseExtraSpellInfo(const std::vector<std::string_view>& tokens,
                                    size_t start_index,
                                    uint32_t& spell_id,
                                    std::string_view& spell_name,
                                    uint32_t& spell_school) {
        spell_id = parseUint32(tokens[start_index]);
        spell_name = tokens[start_index + 1];
        spell_school = parseUint32(tokens[start_index + 2]);
    }

} // namespace util

// =============================================================================
// SPELL_DAMAGE / SPELL_PERIODIC_DAMAGE Parser
// Token count: 44
// Structure: base(11) + spell(3) + advanced(19) + damage_suffix(11)
// Position indices: 28(x), 29(y), 30(map), 31(facing), 32(level)
// Damage suffix starts at token 33
// =============================================================================

struct SpellDamageTag {};
struct SpellPeriodicDamageTag {};

template <>
struct EventParser<SpellDamageTag> {
    static constexpr size_t expected_token_count() { return 44; }

    static DamageEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DamageEventData data{};
        PARSER_VALIDATE("SPELL_DAMAGE", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Parse advanced info (includes position)
        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        // Damage suffix starts at index 33 (after advanced params which end at 32)
        // Suffix: amount, baseAmount, overkill, school, resisted, blocked, absorbed, critical, glancing, crushing, isOffHand
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
        data.is_off_hand = util::parseBool(tokens[DAMAGE_SUFFIX_START + 10]);

        PARSER_FINALIZE("SPELL_DAMAGE", tokens);
        PARSER_DEBUG("SPELL_DAMAGE",
            "spell=" << data.spell.spell_id << " amount=" << data.amount
            << " crit=" << (data.critical ? "yes" : "no")
            << " pos=(" << data.advanced_info.position.x_loc << ", "
            << data.advanced_info.position.y_loc << ")");

        return data;
    }
};

// SPELL_PERIODIC_DAMAGE uses same structure as SPELL_DAMAGE
template <>
struct EventParser<SpellPeriodicDamageTag> : public EventParser<SpellDamageTag> {
    static constexpr size_t expected_token_count() { return 44; }

    static DamageEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        auto data = EventParser<SpellDamageTag>::parse_and_return(tokens);
        PARSER_FINALIZE("SPELL_PERIODIC_DAMAGE", tokens);
        return data;
    }
};

// =============================================================================
// SWING_DAMAGE_LANDED Parser
// Token count: 40 (input), aligned to 43
// Structure: base(11) + [synthetic spell(3)] + advanced(19) + damage_suffix(10)
// Note: SWING events have 10 damage suffix fields (no isOffHand), not 11 like SPELL
// Inserts synthetic spell tokens (1, "Melee", 0x1) to align with SPELL_DAMAGE format
// =============================================================================

struct SwingDamageLandedTag {};

template <>
struct EventParser<SwingDamageLandedTag> {
    static constexpr size_t expected_token_count() { return 40; }

    static DamageEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DamageEventData data{};
        PARSER_VALIDATE("SWING_DAMAGE_LANDED", tokens, expected_token_count(), data);

        // Zero-allocation parsing: parse directly from original tokens
        // SWING events have no spell prefix, so indices are offset by -3 from SPELL events

        // Parse base event data
        uint64_t absolute_ts = EventParser<void>::parse_timestamp_ms(tokens);
        if (get_log_start_timestamp_ms() == 0) {
            get_log_start_timestamp_ms() = absolute_ts;
        }
        int64_t relative_ts = static_cast<int64_t>(absolute_ts) - static_cast<int64_t>(get_log_start_timestamp_ms());
        data.timestamp_ms = static_cast<int32_t>(relative_ts);

        data.source_guid = tokens[TokenIndex::SourceGUID];
        data.source_name = tokens[TokenIndex::SourceName];
        data.source_flags = UnitFlags::parse(tokens[TokenIndex::SourceFlags]);
        data.source_flags.raidMarker = UnitFlags::parseRaidMarker(tokens[TokenIndex::SourceRaidFlags]);
        data.dest_guid = tokens[TokenIndex::DestGUID];
        data.dest_name = tokens[TokenIndex::DestName];
        data.dest_flags = UnitFlags::parse(tokens[TokenIndex::DestFlags]);
        data.dest_flags.raidMarker = UnitFlags::parseRaidMarker(tokens[TokenIndex::DestRaidFlags]);

        // Set synthetic spell info for melee (no allocation needed)
        data.spell.spell_id = 1;
        data.spell.spell_name = "Melee";
        data.spell.spell_school = 1;  // Physical

        // Parse advanced info with offset - SWING has no spell prefix, so indices shift by 3
        constexpr size_t SWING_OFFSET = 3;
        util::parseAdvancedSpellInfoWithOffset(tokens, data.advanced_info, SWING_OFFSET);

        // Damage suffix at index 30 for SWING (not 33 like SPELL)
        // Note: SWING has only 10 damage suffix fields (no isOffHand)
        // position 0 = mitigated (post-armor/absorb) amount
        // position 1 = baseAmount (unmitigated, before any mitigation)
        constexpr size_t DAMAGE_SUFFIX_START = 30;
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
        data.is_off_hand = false;  // SWING events don't have isOffHand field

        PARSER_FINALIZE("SWING_DAMAGE_LANDED", tokens);
        PARSER_DEBUG("SWING_DAMAGE_LANDED",
            "amount=" << data.amount
            << " crit=" << (data.critical ? "yes" : "no")
            << " pos=(" << data.advanced_info.position.x_loc << ", "
            << data.advanced_info.position.y_loc << ")");

        return data;
    }
};

// =============================================================================
// DAMAGE_SPLIT Parser
// Token count: 44
// Structure: base(11) + spell(3) + advanced(19) + damage_suffix(11)
// Same structure as SPELL_DAMAGE
// =============================================================================

struct DamageSplitTag {};

template <>
struct EventParser<DamageSplitTag> {
    static constexpr size_t expected_token_count() { return 44; }

    static DamageEventData parse_and_return(const std::vector<std::string_view>& tokens) {
        DamageEventData data{};
        PARSER_VALIDATE("DAMAGE_SPLIT", tokens, expected_token_count(), data);

        // Parse base event
        util::parseBaseEvent(tokens, data);

        // Parse spell info
        util::parseSpellInfo(tokens, data.spell);

        // Parse advanced info (includes position)
        util::parseAdvancedSpellInfo(tokens, data.advanced_info);

        // Damage suffix starts at index 33 (after advanced params which end at 32)
        // Suffix: amount, baseAmount, overkill, school, resisted, blocked, absorbed, critical, glancing, crushing, isOffHand
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
        data.is_off_hand = util::parseBool(tokens[DAMAGE_SUFFIX_START + 10]);

        PARSER_FINALIZE("DAMAGE_SPLIT", tokens);
        PARSER_DEBUG("DAMAGE_SPLIT",
            "spell=" << data.spell.spell_id << " amount=" << data.amount);

        return data;
    }
};

} // namespace parser
