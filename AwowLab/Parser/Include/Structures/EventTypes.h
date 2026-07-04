#pragma once
#include <cstdint>

// =============================================================================
// Event Types - All combat log event type identifiers
// =============================================================================

enum class EventType {
    ARENA_MATCH_END,
    ARENA_MATCH_START,
    CHALLENGE_MODE_END,
    CHALLENGE_MODE_START,
    COMBAT_LOG_VERSION,
    COMBATANT_INFO,
    DAMAGE_SPLIT,
    EMOTE,
    ENCOUNTER_END,
    ENCOUNTER_START,
    MAP_CHANGE,
    PARTY_KILL,
    SPELL_ABSORBED,
    SPELL_ABSORBED_SUPPORT,
    SPELL_AURA_APPLIED,
    SPELL_AURA_APPLIED_DOSE,
    SPELL_AURA_BROKEN_SPELL,
    SPELL_AURA_REFRESH,
    SPELL_AURA_REMOVED,
    SPELL_AURA_REMOVED_DOSE,
    SPELL_CAST_FAILED,
    SPELL_CAST_START,
    SPELL_CAST_SUCCESS,
    SPELL_DAMAGE,
    SPELL_DAMAGE_SUPPORT,
    SPELL_DISPEL,
    SPELL_EMPOWER_END,
    SPELL_EMPOWER_INTERRUPT,
    SPELL_EMPOWER_START,
    SPELL_ENERGIZE,
    SPELL_HEAL,
    SPELL_HEAL_SUPPORT,
    SPELL_INTERRUPT,
    SPELL_MISSED,
    SPELL_PERIODIC_DAMAGE,
    SPELL_PERIODIC_DAMAGE_SUPPORT,
    SPELL_PERIODIC_ENERGIZE,
    SPELL_PERIODIC_HEAL,
    SPELL_PERIODIC_HEAL_SUPPORT,
    SPELL_PERIODIC_MISSED,
    SPELL_RESURRECT,
    SPELL_SUMMON,
    SWING_DAMAGE,
    SWING_DAMAGE_LANDED,
    SWING_DAMAGE_LANDED_SUPPORT,
    SWING_MISSED,
    UNIT_DIED,
    ZONE_CHANGE,

    Unknown
};

// =============================================================================
// Power Types - Resource types used by different classes
// =============================================================================

enum class PowerType : int8_t {
    HEALTH,                         // -2
    MANA,                           // 0
    RAGE,                           // 1
    FOCUS,                          // 2
    ENERGY,                         // 3
    COMBO_POINTS,                   // 4
    RUNES,                          // 5 - Rune types obsolete
    RUNIC_POWER,                    // 6
    SOUL_SHARDS,                    // 7
    LUNAR_POWER,                    // 8
    HOLY_POWER,                     // 9
    ALTERNATE_POWER,                // 10
    MAELSTROM,                      // 11
    CHI,                            // 12
    INSANITY,                       // 13
    BURNING_EMBERS,                // 14 - Obsolete
    DEMONIC_FURY,                  // 15 - Obsolete
    ARCANE_CHARGES,                 // 16
    FURY,                           // 17
    PAIN,                           // 18
    ESSENCE,                        // 19
    RUNE_BLOOD,                     // 20
    RUNE_FROST,                     // 21
    RUNE_UNHOLY,                    // 22
    ALTERNATE_QUEST,                // 23
    ALTERNATE_ENCOUNTER,           // 24
    ALTERNATE_MOUNT,               // 25
    MAX_POWERS,                     // 26 - Not an actual power type
    ALL                             // 127 - Not an actual power type
};
