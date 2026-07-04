#pragma once
#include <cstdint>
#include <array>

namespace parser {

// WoW Class Colors (the standard palette from the Warcraft wiki
// "Class colors" page). RGB values normalized to 0-255
struct ClassColor {
    uint8_t r, g, b;

    constexpr ClassColor(uint8_t red, uint8_t green, uint8_t blue)
        : r(red), g(green), b(blue) {}

    // Convert to normalized float [0.0, 1.0]
    constexpr float rf() const { return r / 255.0f; }
    constexpr float gf() const { return g / 255.0f; }
    constexpr float bf() const { return b / 255.0f; }
};

namespace ClassColors {
    constexpr ClassColor DeathKnight{0xC4, 0x1E, 0x3A};  // #C41E3A
    constexpr ClassColor DemonHunter{0xA3, 0x30, 0xC9};  // #A330C9
    constexpr ClassColor Druid      {0xFF, 0x7C, 0x0A};  // #FF7C0A
    constexpr ClassColor Evoker     {0x33, 0x93, 0x7F};  // #33937F
    constexpr ClassColor Hunter     {0xAA, 0xD3, 0x72};  // #AAD372
    constexpr ClassColor Mage       {0x3F, 0xC7, 0xEB};  // #3FC7EB
    constexpr ClassColor Monk       {0x00, 0xFF, 0x98};  // #00FF98
    constexpr ClassColor Paladin    {0xF4, 0x8C, 0xBA};  // #F48CBA
    constexpr ClassColor Priest     {0xFF, 0xFF, 0xFF};  // #FFFFFF
    constexpr ClassColor Rogue      {0xFF, 0xF4, 0x68};  // #FFF468
    constexpr ClassColor Shaman     {0x00, 0x70, 0xDD};  // #0070DD
    constexpr ClassColor Warlock    {0x87, 0x88, 0xEE};  // #8788EE
    constexpr ClassColor Warrior    {0xC6, 0x9B, 0x6D};  // #C69B6D
    constexpr ClassColor Unknown    {0x80, 0x80, 0x80};  // #808080 (gray fallback)
}

// Get class color from SpecializationID
// Reference: Warcraft wiki "SpecializationID" page
inline ClassColor getColorFromSpecId(uint16_t spec_id) {
    switch (spec_id) {
        // Death Knight: Blood=250, Frost=251, Unholy=252, Initial=1455
        case 250: case 251: case 252: case 1455:
            return ClassColors::DeathKnight;

        // Demon Hunter: Havoc=577, Vengeance=581, Devourer=1480, Initial=1456
        case 577: case 581: case 1480: case 1456:
            return ClassColors::DemonHunter;

        // Druid: Balance=102, Feral=103, Guardian=104, Restoration=105, Initial=1447
        case 102: case 103: case 104: case 105: case 1447:
            return ClassColors::Druid;

        // Evoker: Devastation=1467, Preservation=1468, Augmentation=1473, Initial=1465
        case 1467: case 1468: case 1473: case 1465:
            return ClassColors::Evoker;

        // Hunter: Beast Mastery=253, Marksmanship=254, Survival=255, Initial=1448
        case 253: case 254: case 255: case 1448:
            return ClassColors::Hunter;

        // Mage: Arcane=62, Fire=63, Frost=64, Initial=1449
        case 62: case 63: case 64: case 1449:
            return ClassColors::Mage;

        // Monk: Brewmaster=268, Mistweaver=270, Windwalker=269, Initial=1450
        case 268: case 269: case 270: case 1450:
            return ClassColors::Monk;

        // Paladin: Holy=65, Protection=66, Retribution=70, Initial=1451
        case 65: case 66: case 70: case 1451:
            return ClassColors::Paladin;

        // Priest: Discipline=256, Holy=257, Shadow=258, Initial=1452
        case 256: case 257: case 258: case 1452:
            return ClassColors::Priest;

        // Rogue: Assassination=259, Outlaw=260, Subtlety=261, Initial=1453
        case 259: case 260: case 261: case 1453:
            return ClassColors::Rogue;

        // Shaman: Elemental=262, Enhancement=263, Restoration=264, Initial=1444
        case 262: case 263: case 264: case 1444:
            return ClassColors::Shaman;

        // Warlock: Affliction=265, Demonology=266, Destruction=267, Initial=1454
        case 265: case 266: case 267: case 1454:
            return ClassColors::Warlock;

        // Warrior: Arms=71, Fury=72, Protection=73, Initial=1446
        case 71: case 72: case 73: case 1446:
            return ClassColors::Warrior;

        default:
            return ClassColors::Unknown;
    }
}

// Get spec name from SpecializationID (useful for UI display)
inline const char* getSpecName(uint16_t spec_id) {
    switch (spec_id) {
        // Death Knight
        case 250: return "Blood";
        case 251: return "Frost";
        case 252: return "Unholy";
        case 1455: return "Death Knight";

        // Demon Hunter
        case 577: return "Havoc";
        case 581: return "Vengeance";
        case 1480: return "Devourer";
        case 1456: return "Demon Hunter";

        // Druid
        case 102: return "Balance";
        case 103: return "Feral";
        case 104: return "Guardian";
        case 105: return "Restoration";
        case 1447: return "Druid";

        // Evoker
        case 1467: return "Devastation";
        case 1468: return "Preservation";
        case 1473: return "Augmentation";
        case 1465: return "Evoker";

        // Hunter
        case 253: return "Beast Mastery";
        case 254: return "Marksmanship";
        case 255: return "Survival";
        case 1448: return "Hunter";

        // Mage
        case 62: return "Arcane";
        case 63: return "Fire";
        case 64: return "Frost";
        case 1449: return "Mage";

        // Monk
        case 268: return "Brewmaster";
        case 269: return "Windwalker";
        case 270: return "Mistweaver";
        case 1450: return "Monk";

        // Paladin
        case 65: return "Holy";
        case 66: return "Protection";
        case 70: return "Retribution";
        case 1451: return "Paladin";

        // Priest
        case 256: return "Discipline";
        case 257: return "Holy";
        case 258: return "Shadow";
        case 1452: return "Priest";

        // Rogue
        case 259: return "Assassination";
        case 260: return "Outlaw";
        case 261: return "Subtlety";
        case 1453: return "Rogue";

        // Shaman
        case 262: return "Elemental";
        case 263: return "Enhancement";
        case 264: return "Restoration";
        case 1444: return "Shaman";

        // Warlock
        case 265: return "Affliction";
        case 266: return "Demonology";
        case 267: return "Destruction";
        case 1454: return "Warlock";

        // Warrior
        case 71: return "Arms";
        case 72: return "Fury";
        case 73: return "Protection";
        case 1446: return "Warrior";

        default: return "Unknown";
    }
}

// Get class name from SpecializationID
inline const char* getClassName(uint16_t spec_id) {
    switch (spec_id) {
        case 250: case 251: case 252: case 1455:
            return "Death Knight";
        case 577: case 581: case 1480: case 1456:
            return "Demon Hunter";
        case 102: case 103: case 104: case 105: case 1447:
            return "Druid";
        case 1467: case 1468: case 1473: case 1465:
            return "Evoker";
        case 253: case 254: case 255: case 1448:
            return "Hunter";
        case 62: case 63: case 64: case 1449:
            return "Mage";
        case 268: case 269: case 270: case 1450:
            return "Monk";
        case 65: case 66: case 70: case 1451:
            return "Paladin";
        case 256: case 257: case 258: case 1452:
            return "Priest";
        case 259: case 260: case 261: case 1453:
            return "Rogue";
        case 262: case 263: case 264: case 1444:
            return "Shaman";
        case 265: case 266: case 267: case 1454:
            return "Warlock";
        case 71: case 72: case 73: case 1446:
            return "Warrior";
        default:
            return "Unknown";
    }
}

} // namespace parser
