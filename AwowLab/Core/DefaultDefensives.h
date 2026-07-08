#pragma once
#include <cstdint>
#include <vector>

// Default seed list of well-known major defensive spell ids, used to
// populate the death recap's "tracked defensives" the first time the app
// runs (see UnifiedSettings::trackedDefensiveSpellIds). Users edit the list
// afterwards in Settings; this is only the starting point.
//
// The ids below are Blizzard game-data spell ids for widely-known personal
// defensives, one to a few per class, gathered from general familiarity with
// the game. This is a hand-written starter set, not a copy of any third
// party's curated data.
//
// IMPORTANT: these must be the BUFF (aura) ids, not the cast/ability ids.
// The death recap matches against SPELL_AURA_APPLIED, so a cast id that
// differs from the buff id (e.g. Blur casts 198589 but applies aura 212800)
// would never match and the defensive would silently never show. When adding
// an id, take it from a SPELL_AURA_APPLIED line in a real log, not the
// tooltip/cast id.
namespace awow {

inline const std::vector<uint32_t>& defaultDefensiveSpellIds() {
    static const std::vector<uint32_t> ids = {
        // Warrior
        871,     // Shield Wall
        12975,   // Last Stand
        23920,   // Spell Reflection
        118038,  // Die by the Sword
        184364,  // Enraged Regeneration

        // Paladin
        642,     // Divine Shield
        403876,  // Divine Protection (Prot buff id; base/cast is 498)
        86659,   // Guardian of Ancient Kings
        31850,   // Ardent Defender
        184662,  // Shield of Vengeance

        // Death Knight
        48792,   // Icebound Fortitude
        55233,   // Vampiric Blood
        48707,   // Anti-Magic Shell
        49028,   // Dancing Rune Weapon

        // Hunter
        186265,  // Aspect of the Turtle
        109304,  // Exhilaration

        // Rogue
        31224,   // Cloak of Shadows
        5277,    // Evasion
        1966,    // Feint

        // Priest
        47585,   // Dispersion
        19236,   // Desperate Prayer
        33206,   // Pain Suppression
        47788,   // Guardian Spirit

        // Shaman
        108271,  // Astral Shift
        198103,  // Earth Elemental

        // Mage
        45438,   // Ice Block
        11426,   // Ice Barrier
        235313,  // Blazing Barrier
        235450,  // Prismatic Barrier
        55342,   // Mirror Image

        // Warlock
        104773,  // Unending Resolve
        108416,  // Dark Pact

        // Monk
        120954,  // Fortifying Brew (buff id; cast is 115203)
        122470,  // Touch of Karma
        122783,  // Diffuse Magic
        122278,  // Dampen Harm

        // Druid
        22812,   // Barkskin
        61336,   // Survival Instincts
        102342,  // Ironbark

        // Demon Hunter
        212800,  // Blur (buff id; cast is 198589)
        187827,  // Metamorphosis (Vengeance)
        196555,  // Netherwalk

        // Evoker
        363916,  // Obsidian Scales
        374349,  // Renewing Blaze (buff id; cast is 374348)
    };
    return ids;
}

}  // namespace awow
