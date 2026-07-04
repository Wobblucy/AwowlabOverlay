#pragma once
#include <cstdint>
#include <charconv>
#include <string_view>

// =============================================================================
// UnitFlags - Parsed from combat log sourceFlags/destFlags (e.g., "0x511")
// Reference: the warcraft wiki "UnitFlag" article
// =============================================================================

// Affiliation: Relationship to the logging player (mutually exclusive)
enum class UnitAffiliation : uint8_t {
    Mine = 0,      // 0x00000001 - Player, their pet, or mind-controlled unit
    Party = 1,     // 0x00000002 - Party member
    Raid = 2,      // 0x00000004 - Raid member
    Outsider = 3   // 0x00000008 - No relationship
};

// Reaction: How the unit reacts to the logging player (mutually exclusive)
enum class UnitReaction : uint8_t {
    Friendly = 0,  // 0x00000010 - Allied
    Neutral = 1,   // 0x00000020 - Neutral
    Hostile = 2    // 0x00000040 - Hostile
};

// Controller: Who controls the unit (mutually exclusive)
enum class UnitController : uint8_t {
    Player = 0,    // 0x00000100 - Player-controlled (includes player pets)
    NPC = 1        // 0x00000200 - NPC-controlled
};

// UnitType: Classification of the unit (mutually exclusive)
enum class UnitType : uint8_t {
    Player = 0,    // 0x00000400 - Player character
    NPC = 1,       // 0x00000800 - NPC
    Pet = 2,       // 0x00001000 - Combat pet
    Guardian = 3,  // 0x00002000 - Guardian (temporary summon)
    Object = 4     // 0x00004000 - Trap, totem, etc.
};

// Raid markers (from raidFlags parameter, separate from unitFlags)
enum class RaidMarker : uint8_t {
    None = 0,
    Star = 1,
    Circle = 2,
    Diamond = 3,
    Triangle = 4,
    Moon = 5,
    Square = 6,
    Cross = 7,
    Skull = 8
};

// Bitmask constants for parsing
namespace UnitFlagMasks {
    // Affiliation (bits 0-3)
    constexpr uint32_t AFFILIATION_MINE     = 0x00000001;
    constexpr uint32_t AFFILIATION_PARTY    = 0x00000002;
    constexpr uint32_t AFFILIATION_RAID     = 0x00000004;
    constexpr uint32_t AFFILIATION_OUTSIDER = 0x00000008;
    constexpr uint32_t AFFILIATION_MASK     = 0x0000000F;

    // Reaction (bits 4-7)
    constexpr uint32_t REACTION_FRIENDLY    = 0x00000010;
    constexpr uint32_t REACTION_NEUTRAL     = 0x00000020;
    constexpr uint32_t REACTION_HOSTILE     = 0x00000040;
    constexpr uint32_t REACTION_MASK        = 0x000000F0;

    // Controller (bits 8-9)
    constexpr uint32_t CONTROLLER_PLAYER    = 0x00000100;
    constexpr uint32_t CONTROLLER_NPC       = 0x00000200;
    constexpr uint32_t CONTROLLER_MASK      = 0x00000300;

    // Type (bits 10-15)
    constexpr uint32_t TYPE_PLAYER          = 0x00000400;
    constexpr uint32_t TYPE_NPC             = 0x00000800;
    constexpr uint32_t TYPE_PET             = 0x00001000;
    constexpr uint32_t TYPE_GUARDIAN        = 0x00002000;
    constexpr uint32_t TYPE_OBJECT          = 0x00004000;
    constexpr uint32_t TYPE_MASK            = 0x0000FC00;

    // Special flags (bits 16+)
    constexpr uint32_t SPECIAL_TARGET       = 0x00010000;
    constexpr uint32_t SPECIAL_FOCUS        = 0x00020000;
    constexpr uint32_t SPECIAL_MAINTANK     = 0x00040000;
    constexpr uint32_t SPECIAL_MAINASSIST   = 0x00080000;
    constexpr uint32_t SPECIAL_NONE         = 0x80000000;  // Unit doesn't exist
    constexpr uint32_t SPECIAL_MASK         = 0xFFFF0000;

    // Raid target markers (from separate raidFlags parameter)
    constexpr uint32_t RAIDMARKER_STAR      = 0x00000001;
    constexpr uint32_t RAIDMARKER_CIRCLE    = 0x00000002;
    constexpr uint32_t RAIDMARKER_DIAMOND   = 0x00000004;
    constexpr uint32_t RAIDMARKER_TRIANGLE  = 0x00000008;
    constexpr uint32_t RAIDMARKER_MOON      = 0x00000010;
    constexpr uint32_t RAIDMARKER_SQUARE    = 0x00000020;
    constexpr uint32_t RAIDMARKER_CROSS     = 0x00000040;
    constexpr uint32_t RAIDMARKER_SKULL     = 0x00000080;
}

struct UnitFlags {
    uint32_t raw;              // Original bitmask value for debugging
    UnitAffiliation affiliation;
    UnitReaction reaction;
    UnitController controller;
    UnitType unitType;
    RaidMarker raidMarker;     // From separate raidFlags parameter

    // Special flags (non-exclusive)
    bool isTarget : 1;
    bool isFocus : 1;
    bool isMainTank : 1;
    bool isMainAssist : 1;
    bool exists : 1;           // False if SPECIAL_NONE flag is set

    // Default constructor - unknown/neutral state
    UnitFlags()
        : raw(0)
        , affiliation(UnitAffiliation::Outsider)
        , reaction(UnitReaction::Neutral)
        , controller(UnitController::NPC)
        , unitType(UnitType::NPC)
        , raidMarker(RaidMarker::None)
        , isTarget(false)
        , isFocus(false)
        , isMainTank(false)
        , isMainAssist(false)
        , exists(true)
    {}

    // Parse from hex string like "0x511" or "0x10a48"
    static UnitFlags parse(std::string_view hexStr) {
        UnitFlags flags;

        // Skip "0x" or "0X" prefix if present
        if (hexStr.size() >= 2 && hexStr[0] == '0' && (hexStr[1] == 'x' || hexStr[1] == 'X')) {
            hexStr = hexStr.substr(2);
        }

        uint32_t value = 0;
        auto result = std::from_chars(hexStr.data(), hexStr.data() + hexStr.size(), value, 16);
        if (result.ec != std::errc()) {
            return flags;  // Return default on parse error
        }

        flags.raw = value;

        // Parse affiliation (mutually exclusive)
        uint32_t aff = value & UnitFlagMasks::AFFILIATION_MASK;
        if (aff & UnitFlagMasks::AFFILIATION_MINE)        flags.affiliation = UnitAffiliation::Mine;
        else if (aff & UnitFlagMasks::AFFILIATION_PARTY)  flags.affiliation = UnitAffiliation::Party;
        else if (aff & UnitFlagMasks::AFFILIATION_RAID)   flags.affiliation = UnitAffiliation::Raid;
        else                                              flags.affiliation = UnitAffiliation::Outsider;

        // Parse reaction (mutually exclusive)
        uint32_t react = value & UnitFlagMasks::REACTION_MASK;
        if (react & UnitFlagMasks::REACTION_FRIENDLY)     flags.reaction = UnitReaction::Friendly;
        else if (react & UnitFlagMasks::REACTION_NEUTRAL) flags.reaction = UnitReaction::Neutral;
        else if (react & UnitFlagMasks::REACTION_HOSTILE) flags.reaction = UnitReaction::Hostile;

        // Parse controller (mutually exclusive)
        uint32_t ctrl = value & UnitFlagMasks::CONTROLLER_MASK;
        if (ctrl & UnitFlagMasks::CONTROLLER_PLAYER)      flags.controller = UnitController::Player;
        else                                              flags.controller = UnitController::NPC;

        // Parse unit type (mutually exclusive)
        uint32_t type = value & UnitFlagMasks::TYPE_MASK;
        if (type & UnitFlagMasks::TYPE_PLAYER)            flags.unitType = UnitType::Player;
        else if (type & UnitFlagMasks::TYPE_PET)          flags.unitType = UnitType::Pet;
        else if (type & UnitFlagMasks::TYPE_GUARDIAN)     flags.unitType = UnitType::Guardian;
        else if (type & UnitFlagMasks::TYPE_OBJECT)       flags.unitType = UnitType::Object;
        else                                              flags.unitType = UnitType::NPC;

        // Parse special flags (non-exclusive)
        flags.isTarget     = (value & UnitFlagMasks::SPECIAL_TARGET) != 0;
        flags.isFocus      = (value & UnitFlagMasks::SPECIAL_FOCUS) != 0;
        flags.isMainTank   = (value & UnitFlagMasks::SPECIAL_MAINTANK) != 0;
        flags.isMainAssist = (value & UnitFlagMasks::SPECIAL_MAINASSIST) != 0;
        flags.exists       = (value & UnitFlagMasks::SPECIAL_NONE) == 0;

        return flags;
    }

    // Parse raid marker from separate raidFlags parameter
    static RaidMarker parseRaidMarker(std::string_view hexStr) {
        if (hexStr.size() >= 2 && hexStr[0] == '0' && (hexStr[1] == 'x' || hexStr[1] == 'X')) {
            hexStr = hexStr.substr(2);
        }

        uint32_t value = 0;
        auto result = std::from_chars(hexStr.data(), hexStr.data() + hexStr.size(), value, 16);
        if (result.ec != std::errc() || value == 0) {
            return RaidMarker::None;
        }

        if (value & UnitFlagMasks::RAIDMARKER_STAR)     return RaidMarker::Star;
        if (value & UnitFlagMasks::RAIDMARKER_CIRCLE)   return RaidMarker::Circle;
        if (value & UnitFlagMasks::RAIDMARKER_DIAMOND)  return RaidMarker::Diamond;
        if (value & UnitFlagMasks::RAIDMARKER_TRIANGLE) return RaidMarker::Triangle;
        if (value & UnitFlagMasks::RAIDMARKER_MOON)     return RaidMarker::Moon;
        if (value & UnitFlagMasks::RAIDMARKER_SQUARE)   return RaidMarker::Square;
        if (value & UnitFlagMasks::RAIDMARKER_CROSS)    return RaidMarker::Cross;
        if (value & UnitFlagMasks::RAIDMARKER_SKULL)    return RaidMarker::Skull;

        return RaidMarker::None;
    }

    // Convenience query methods
    bool isHostile() const { return reaction == UnitReaction::Hostile; }
    bool isFriendly() const { return reaction == UnitReaction::Friendly; }
    bool isNeutral() const { return reaction == UnitReaction::Neutral; }

    bool isPlayerType() const { return unitType == UnitType::Player; }
    bool isPet() const { return unitType == UnitType::Pet; }
    bool isGuardian() const { return unitType == UnitType::Guardian; }
    bool isNPC() const { return unitType == UnitType::NPC; }
    bool isObject() const { return unitType == UnitType::Object; }

    bool isPlayerControlled() const { return controller == UnitController::Player; }
    bool isNPCControlled() const { return controller == UnitController::NPC; }

    bool isInGroup() const {
        return affiliation == UnitAffiliation::Mine ||
               affiliation == UnitAffiliation::Party ||
               affiliation == UnitAffiliation::Raid;
    }
    bool isOutsider() const { return affiliation == UnitAffiliation::Outsider; }

    // Combined checks for common filtering scenarios
    bool isFriendlyPlayer() const { return isFriendly() && isPlayerType(); }
    bool isHostileNPC() const { return isHostile() && isNPC(); }
    bool isFriendlyPetOrGuardian() const { return isFriendly() && (isPet() || isGuardian()); }
};
