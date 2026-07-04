#pragma once

#include <cstdint>

namespace parser {

// WoW player classes (matches game order)
enum class WowClass : uint8_t {
    Unknown = 0,
    DeathKnight,
    DemonHunter,
    Druid,
    Evoker,
    Hunter,
    Mage,
    Monk,
    Paladin,
    Priest,
    Rogue,
    Shaman,
    Warlock,
    Warrior,
    COUNT
};

// Convert SpecializationID to WowClass
inline WowClass getClassFromSpecId(uint16_t spec_id) {
    switch (spec_id) {
        // Death Knight: Blood=250, Frost=251, Unholy=252, Initial=1455
        case 250: case 251: case 252: case 1455:
            return WowClass::DeathKnight;

        // Demon Hunter: Havoc=577, Vengeance=581, Devourer=1480, Initial=1456
        case 577: case 581: case 1480: case 1456:
            return WowClass::DemonHunter;

        // Druid: Balance=102, Feral=103, Guardian=104, Restoration=105, Initial=1447
        case 102: case 103: case 104: case 105: case 1447:
            return WowClass::Druid;

        // Evoker: Devastation=1467, Preservation=1468, Augmentation=1473, Initial=1465
        case 1467: case 1468: case 1473: case 1465:
            return WowClass::Evoker;

        // Hunter: Beast Mastery=253, Marksmanship=254, Survival=255, Initial=1448
        case 253: case 254: case 255: case 1448:
            return WowClass::Hunter;

        // Mage: Arcane=62, Fire=63, Frost=64, Initial=1449
        case 62: case 63: case 64: case 1449:
            return WowClass::Mage;

        // Monk: Brewmaster=268, Mistweaver=270, Windwalker=269, Initial=1450
        case 268: case 269: case 270: case 1450:
            return WowClass::Monk;

        // Paladin: Holy=65, Protection=66, Retribution=70, Initial=1451
        case 65: case 66: case 70: case 1451:
            return WowClass::Paladin;

        // Priest: Discipline=256, Holy=257, Shadow=258, Initial=1452
        case 256: case 257: case 258: case 1452:
            return WowClass::Priest;

        // Rogue: Assassination=259, Outlaw=260, Subtlety=261, Initial=1453
        case 259: case 260: case 261: case 1453:
            return WowClass::Rogue;

        // Shaman: Elemental=262, Enhancement=263, Restoration=264, Initial=1444
        case 262: case 263: case 264: case 1444:
            return WowClass::Shaman;

        // Warlock: Affliction=265, Demonology=266, Destruction=267, Initial=1454
        case 265: case 266: case 267: case 1454:
            return WowClass::Warlock;

        // Warrior: Arms=71, Fury=72, Protection=73, Initial=1446
        case 71: case 72: case 73: case 1446:
            return WowClass::Warrior;

        default:
            return WowClass::Unknown;
    }
}

// Get display name for WowClass
inline const char* getWowClassName(WowClass wowClass) {
    switch (wowClass) {
        case WowClass::DeathKnight: return "Death Knight";
        case WowClass::DemonHunter: return "Demon Hunter";
        case WowClass::Druid:       return "Druid";
        case WowClass::Evoker:      return "Evoker";
        case WowClass::Hunter:      return "Hunter";
        case WowClass::Mage:        return "Mage";
        case WowClass::Monk:        return "Monk";
        case WowClass::Paladin:     return "Paladin";
        case WowClass::Priest:      return "Priest";
        case WowClass::Rogue:       return "Rogue";
        case WowClass::Shaman:      return "Shaman";
        case WowClass::Warlock:     return "Warlock";
        case WowClass::Warrior:     return "Warrior";
        default:                    return "Unknown";
    }
}

} // namespace parser
