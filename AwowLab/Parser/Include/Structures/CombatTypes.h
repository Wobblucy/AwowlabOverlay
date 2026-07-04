#pragma once
#include <cstdint>
#include <string_view>

// =============================================================================
// Combat Event Types and Flags
// =============================================================================

enum class CombatEventType : uint8_t {
    DamageDealt = 0,
    HealingDone = 1
};

namespace CombatEventFlags {
    constexpr uint16_t Critical    = 0x0001;
    constexpr uint16_t Periodic    = 0x0002;  // DOT/HOT tick
    constexpr uint16_t Absorbed    = 0x0004;  // Had absorb component
    constexpr uint16_t Overkill    = 0x0008;  // Damage exceeded target HP
    constexpr uint16_t Overheal    = 0x0010;  // Heal exceeded target max HP
    constexpr uint16_t Blocked     = 0x0020;  // Had block component
    constexpr uint16_t Resisted    = 0x0040;  // Had resist component
    constexpr uint16_t IsSwing     = 0x0080;  // Melee auto-attack (spell_id = 0)
    constexpr uint16_t Glancing    = 0x0100;  // Glancing blow
    constexpr uint16_t Crushing    = 0x0200;  // Crushing blow
    constexpr uint16_t OffHand     = 0x0400;  // Off-hand weapon attack
    constexpr uint16_t Support     = 0x0800;  // From SUPPORT event (Aug Evoker attribution)
    constexpr uint16_t IsAbsorb    = 0x1000;  // Healing from absorb shield (not direct heal)
    // Damage between friendly units (self-damage, Spirit Link, etc.)
    // Counts toward the target's damage taken but not the source's
    // damage done - same accounting as the Details! addon
    constexpr uint16_t FriendlyFire = 0x2000;
}

// =============================================================================
// Spell School Bitmask Values
// Reference: the wowpedia "Spell school" article
// =============================================================================

namespace SpellSchool {
    constexpr uint32_t Physical  = 0x01;
    constexpr uint32_t Holy      = 0x02;
    constexpr uint32_t Fire      = 0x04;
    constexpr uint32_t Nature    = 0x08;
    constexpr uint32_t Frost     = 0x10;
    constexpr uint32_t Shadow    = 0x20;
    constexpr uint32_t Arcane    = 0x40;

    // Common combinations
    constexpr uint32_t Holystrike     = Holy | Physical;      // 0x03
    constexpr uint32_t Flamestrike    = Fire | Physical;      // 0x05
    constexpr uint32_t Stormstrike    = Nature | Physical;    // 0x09
    constexpr uint32_t Froststrike    = Frost | Physical;     // 0x11
    constexpr uint32_t Shadowstrike   = Shadow | Physical;    // 0x21
    constexpr uint32_t Spellstrike    = Arcane | Physical;    // 0x41
    constexpr uint32_t Divine         = Holy | Arcane;        // 0x42
    constexpr uint32_t Spellfire      = Fire | Arcane;        // 0x44
    constexpr uint32_t Spellstorm     = Nature | Arcane;      // 0x48
    constexpr uint32_t Spellfrost     = Frost | Arcane;       // 0x50
    constexpr uint32_t Spellshadow    = Shadow | Arcane;      // 0x60
    constexpr uint32_t Elemental      = Fire | Nature | Frost; // 0x1C
    constexpr uint32_t Chromatic      = Holy | Fire | Nature | Frost | Shadow | Arcane; // 0x7E
    constexpr uint32_t Cosmic         = Holy | Nature | Shadow | Arcane; // 0x6A
    constexpr uint32_t Chaos          = Physical | Holy | Fire | Nature | Frost | Shadow | Arcane; // 0x7F
}

// =============================================================================
// Miss Types for _MISSED Events
// =============================================================================

enum class MissType : uint8_t {
    ABSORB,     // Damage absorbed by a shield
    BLOCK,      // Blocked (melee only)
    DEFLECT,    // Deflected
    DODGE,      // Dodged
    EVADE,      // Evaded (mob out of combat)
    IMMUNE,     // Target is immune
    MISS,       // Standard miss
    PARRY,      // Parried
    REFLECT,    // Spell reflected
    RESIST,     // Fully resisted
    Unknown
};

inline MissType parseMissType(std::string_view str) {
    if (str == "ABSORB") return MissType::ABSORB;
    if (str == "BLOCK") return MissType::BLOCK;
    if (str == "DEFLECT") return MissType::DEFLECT;
    if (str == "DODGE") return MissType::DODGE;
    if (str == "EVADE") return MissType::EVADE;
    if (str == "IMMUNE") return MissType::IMMUNE;
    if (str == "MISS") return MissType::MISS;
    if (str == "PARRY") return MissType::PARRY;
    if (str == "REFLECT") return MissType::REFLECT;
    if (str == "RESIST") return MissType::RESIST;
    return MissType::Unknown;
}

// =============================================================================
// Aura Types
// =============================================================================

enum class AuraType : uint8_t {
    BUFF,
    DEBUFF,
    Unknown
};

inline AuraType parseAuraType(std::string_view str) {
    if (str == "BUFF") return AuraType::BUFF;
    if (str == "DEBUFF") return AuraType::DEBUFF;
    return AuraType::Unknown;
}

// =============================================================================
// Environmental Damage Types
// =============================================================================

enum class EnvironmentalType : uint8_t {
    Drowning,
    Falling,
    Fatigue,
    Fire,
    Lava,
    Slime,
    Unknown
};

inline EnvironmentalType parseEnvironmentalType(std::string_view str) {
    if (str == "Drowning") return EnvironmentalType::Drowning;
    if (str == "Falling") return EnvironmentalType::Falling;
    if (str == "Fatigue") return EnvironmentalType::Fatigue;
    if (str == "Fire") return EnvironmentalType::Fire;
    if (str == "Lava") return EnvironmentalType::Lava;
    if (str == "Slime") return EnvironmentalType::Slime;
    return EnvironmentalType::Unknown;
}
