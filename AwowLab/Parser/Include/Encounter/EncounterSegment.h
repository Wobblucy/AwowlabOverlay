#pragma once
#include <string>
#include <cstdint>
#include <vector>

// Type of segment (M+ dungeon vs boss encounter)
enum class SegmentType : uint8_t {
    BossEncounter,      // Regular boss encounter (ENCOUNTER_START/END)
    MythicPlusDungeon   // Full M+ dungeon run (CHALLENGE_MODE_START/END)
};

// Get human-readable difficulty name from WoW difficulty ID
inline const char* getDifficultyName(uint32_t difficultyId) {
    switch (difficultyId) {
        case 1:  return "Normal";
        case 2:  return "Heroic";
        case 3:  return "10 Player";
        case 4:  return "25 Player";
        case 5:  return "10 Heroic";
        case 6:  return "25 Heroic";
        case 7:  return "LFR";
        case 8:  return "Mythic Keystone";
        case 14: return "Normal";
        case 15: return "Heroic";
        case 16: return "Mythic";
        case 17: return "LFR";
        case 23: return "Mythic";
        default: return "Unknown";
    }
}

// Represents a combat encounter segment in the log file
struct EncounterSegment {
    size_t startLineIndex;      // Index of first line in encounter (after ENCOUNTER_START or CHALLENGE_MODE_START)
    size_t endLineIndex;        // Index of last line in encounter (before ENCOUNTER_END or CHALLENGE_MODE_END)

    // Byte offsets for direct file access (allows tokenizing just this segment)
    size_t startByteOffset;     // Byte offset of first line in encounter
    size_t endByteOffset;       // Byte offset past end of last line (for calculating range)

    uint32_t encounterId;       // WoW encounter ID (0 for M+ dungeons)
    std::string encounterName;  // Boss/encounter name or dungeon name
    uint32_t difficultyId;      // Difficulty ID (normal, heroic, mythic, etc.)
    uint32_t groupSize;         // Raid/party size

    int32_t startTimestamp_ms;  // Timestamp when encounter started (negative = before log start)
    int32_t endTimestamp_ms;    // Timestamp when encounter ended
    bool isSuccess;             // True if encounter completed successfully

    // M+ specific fields
    SegmentType segmentType;    // Whether this is a boss encounter or M+ dungeon
    uint32_t keystoneLevel;     // M+ keystone level (0 for non-M+)
    uint32_t instanceId;        // Instance ID for M+ dungeons
    uint32_t challengeModeId;   // Challenge mode ID for M+ dungeons
    std::vector<uint32_t> affixIds;  // Active affixes for M+ dungeons

    // For M+ dungeons, track sub-encounters (bosses within the dungeon)
    std::vector<size_t> subEncounterIndices;  // Indices into parent encounter list

    // Constructor for easy initialization
    EncounterSegment()
        : startLineIndex(0)
        , endLineIndex(0)
        , startByteOffset(0)
        , endByteOffset(0)
        , encounterId(0)
        , encounterName("Unknown")
        , difficultyId(0)
        , groupSize(0)
        , startTimestamp_ms(0)
        , endTimestamp_ms(0)
        , isSuccess(false)
        , segmentType(SegmentType::BossEncounter)
        , keystoneLevel(0)
        , instanceId(0)
        , challengeModeId(0)
    {}

    // Check if this is an M+ dungeon segment
    bool isMythicPlus() const { return segmentType == SegmentType::MythicPlusDungeon; }

    // Check if this is a boss encounter
    bool isBossEncounter() const { return segmentType == SegmentType::BossEncounter; }
};
