#pragma once
#include <cstddef>

// =============================================================================
// Base Token Indices (0-indexed) - Common to all events
// =============================================================================

namespace TokenIndex {
    // Base parameters (all events)
    constexpr size_t Date = 0;
    constexpr size_t Time = 1;
    constexpr size_t EventType = 2;
    constexpr size_t SourceGUID = 3;
    constexpr size_t SourceName = 4;
    constexpr size_t SourceFlags = 5;
    constexpr size_t SourceRaidFlags = 6;
    constexpr size_t DestGUID = 7;
    constexpr size_t DestName = 8;
    constexpr size_t DestFlags = 9;
    constexpr size_t DestRaidFlags = 10;

    // Spell prefix (SPELL_*, RANGE_*)
    constexpr size_t SpellID = 11;
    constexpr size_t SpellName = 12;
    constexpr size_t SpellSchool = 13;

    // Advanced Combat Log - SPELL events (after spell prefix at 14-32)
    // Structure: InfoGUID(14), OwnerGUID(15), HP(16-17), Stats(18-21), Power(22-27), Position(28-31), Level(32)
    // Note: WoW wiki says 17 params but actual logs have 19 (includes 2 extra fields at 22-23)
    namespace AdvancedSpell {
        constexpr size_t InfoGUID = 14;
        constexpr size_t OwnerGUID = 15;
        constexpr size_t CurrentHP = 16;
        constexpr size_t MaxHP = 17;
        constexpr size_t AttackPower = 18;
        constexpr size_t SpellPower = 19;
        constexpr size_t Armor = 20;
        constexpr size_t Absorb = 21;
        constexpr size_t Unknown1 = 22;      // Extra field not in wiki
        constexpr size_t Unknown2 = 23;      // Extra field not in wiki
        constexpr size_t PowerTypeIdx = 24;  // Renamed to avoid conflict with PowerType enum
        constexpr size_t CurrentPower = 25;
        constexpr size_t MaxPower = 26;
        constexpr size_t PowerCost = 27;
        constexpr size_t PosX = 28;
        constexpr size_t PosY = 29;
        constexpr size_t MapID = 30;
        constexpr size_t Facing = 31;
        constexpr size_t Level = 32;
    }

    // Note: SWING events now use synthetic spell tokens to align with SPELL indices.
    // The AdvancedSwing namespace has been removed - all events use AdvancedSpell indices.
}
