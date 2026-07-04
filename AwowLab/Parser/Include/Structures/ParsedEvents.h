#pragma once
#include <cstdint>
#include <string_view>
#include "Flags.h"
#include "EventTypes.h"
#include "CombatTypes.h"

// =============================================================================
// Parsed Event Data Structures
// =============================================================================

// Common position data extracted from advanced combat log
struct PositionData {
    float x_loc;             // Raw world coordinate
    float y_loc;             // Raw world coordinate
    uint16_t map_id;
    uint16_t facing;         // Scaled by 10000 (radians * 10000)
    bool valid = false;      // Set to true if position data was extracted
};

// Common unit info from advanced combat log
struct AdvancedUnitInfo {
    std::string_view info_guid;
    std::string_view owner_guid;
    uint64_t current_hp = 0;
    uint64_t max_hp = 0;
    uint32_t attack_power = 0;
    uint32_t spell_power = 0;
    uint32_t armor = 0;
    uint32_t absorb = 0;
    int8_t power_type = -1;
    uint32_t current_power = 0;
    uint32_t max_power = 0;
    uint32_t power_cost = 0;
    uint8_t level = 0;
    PositionData position;
};

// Base event data common to all parsed events
struct BaseEventData {
    int32_t timestamp_ms = 0;           // Relative to log start (negative = before encounter)
    std::string_view source_guid;
    std::string_view source_name;
    std::string_view dest_guid;
    std::string_view dest_name;
    UnitFlags source_flags;
    UnitFlags dest_flags;
};

// Spell prefix data (for SPELL_*, RANGE_* events)
struct SpellInfo {
    uint32_t spell_id = 0;
    std::string_view spell_name;
    uint32_t spell_school = 0;
};

// =============================================================================
// Damage Event Data
// =============================================================================

struct DamageEventData : public BaseEventData {
    SpellInfo spell;                    // Empty for SWING events
    AdvancedUnitInfo advanced_info;

    // Damage suffix data
    // amount = unmitigated damage (baseAmount from log position 1)
    // mitigated_amount = post-mitigation damage (amount from log position 0)
    int64_t amount = 0;
    int64_t mitigated_amount = 0;
    int64_t overkill = 0;               // -1 if no overkill
    uint32_t damage_school = 0;
    int64_t resisted = 0;
    int64_t blocked = 0;
    int64_t absorbed = 0;
    bool critical = false;
    bool glancing = false;
    bool crushing = false;
    bool is_off_hand = false;
};

// =============================================================================
// Heal Event Data
// =============================================================================

struct HealEventData : public BaseEventData {
    SpellInfo spell;
    AdvancedUnitInfo advanced_info;

    // Heal suffix data
    int64_t amount = 0;
    int64_t overhealing = 0;
    int64_t absorbed = 0;
    bool critical = false;
};

// =============================================================================
// Missed Event Data
// =============================================================================

struct MissedEventData : public BaseEventData {
    SpellInfo spell;                    // Empty for SWING events

    MissType miss_type = MissType::Unknown;
    bool is_off_hand = false;
    int64_t amount_missed = 0;          // For partial resists/absorbs
    int64_t total_pending = 0;          // For partial resists - total damage pending
    int64_t blocked_amount = 0;         // For BLOCK miss type
    bool critical = false;              // For ABSORB miss type
    bool has_position = false;          // Missed events don't have position
};

// =============================================================================
// Aura Event Data
// =============================================================================

struct AuraEventData : public BaseEventData {
    SpellInfo spell;

    AuraType aura_type = AuraType::Unknown;
    int32_t amount = 0;                 // Stack count for dose events, absorb amount for shields
    bool has_position = false;          // Aura events don't have position

    // For SPELL_AURA_BROKEN_SPELL - the spell that broke the aura
    uint32_t extra_spell_id = 0;
    std::string_view extra_spell_name;
    uint32_t extra_spell_school = 0;
};

// =============================================================================
// Energize Event Data
// =============================================================================

struct EnergizeEventData : public BaseEventData {
    SpellInfo spell;
    AdvancedUnitInfo advanced_info;

    double amount = 0.0;                // Can be fractional
    double over_energize = 0.0;
    PowerType power_type = PowerType::MANA;
    int64_t max_power = 0;
};

// =============================================================================
// Cast Event Data
// =============================================================================

struct CastEventData : public BaseEventData {
    SpellInfo spell;
    AdvancedUnitInfo advanced_info;     // Only for SPELL_CAST_SUCCESS

    std::string_view fail_reason;       // Only for SPELL_CAST_FAILED
};

// =============================================================================
// Summon/Resurrect Event Data
// =============================================================================

struct SummonEventData : public BaseEventData {
    SpellInfo spell;
    bool has_position = false;          // Summon events don't have position
};

// =============================================================================
// Death Event Data
// =============================================================================

struct DeathEventData : public BaseEventData {
    int32_t recap_id = 0;               // Death recap ID, 0 if none
    bool unconscious_on_death = false;
    bool has_position = false;          // Death events don't have position

    // For SPELL_INSTAKILL - the killing spell
    uint32_t spell_id = 0;
    std::string_view spell_name;
};

// =============================================================================
// Dispel/Interrupt Event Data
// =============================================================================

struct DispelEventData : public BaseEventData {
    SpellInfo spell;                    // The dispel/interrupt spell

    // The spell that was dispelled/interrupted
    uint32_t extra_spell_id = 0;
    std::string_view extra_spell_name;
    uint32_t extra_spell_school = 0;
    AuraType aura_type = AuraType::Unknown;  // For dispels
    bool has_position = false;          // Dispel events don't have position

    // For SPELL_EXTRA_ATTACKS
    int64_t amount = 0;
};

// =============================================================================
// Absorbed Event Data
// =============================================================================

struct AbsorbedEventData : public BaseEventData {
    SpellInfo spell;                    // The damaging spell (if any)

    // The absorb shield info
    std::string_view absorber_guid;
    std::string_view absorber_name;
    UnitFlags absorber_flags;
    uint32_t absorb_spell_id = 0;
    std::string_view absorb_spell_name;
    uint32_t absorb_spell_school = 0;

    int64_t amount = 0;                 // Amount absorbed
    int64_t total_damage = 0;           // Total damage before absorb
    bool critical = false;
    bool has_position = false;          // Absorb events don't have position

    // For SPELL_ABSORBED_SUPPORT
    std::string_view supporter_guid;
};
