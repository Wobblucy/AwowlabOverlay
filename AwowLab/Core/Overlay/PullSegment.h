#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

// Pull segment type classification for labeling
enum class PullSegmentType : uint8_t {
    TrashPull,       // Individual trash pull
    BossPull,        // Boss encounter
    DungeonOverall,  // Full dungeon (CHALLENGE_MODE_START to CHALLENGE_MODE_END)
    TrashOverall,    // Virtual aggregate (for stats display only)
    EmptyRun,        // A key that was started then abandoned with no combat.
                     // WoW writes no events for an abandoned run, so there's
                     // nothing to parse - it exists only so the run still
                     // shows as a group in the selector.
    Unknown
};

// Represents a single combat pull (trash or boss) in the combat log.
// Stores byte offsets into the log file for efficient navigation and re-parsing.
//
// For bosses, the segment is bounded by ENCOUNTER_START/ENCOUNTER_END.
// For trash, the segment is detected by combat events with idle timeout (5s default).
//
struct PullSegment {
    // File position markers for re-parsing
    size_t startByteOffset = 0;       // First byte of first event in this pull
    size_t endByteOffset = 0;         // Last byte of last event in this pull

    // Timestamps relative to log start (in milliseconds)
    int32_t startTime_ms = 0;         // Timestamp of pull start
    int32_t endTime_ms = 0;           // Timestamp of pull end (0 if still in progress)

    // Pull identification
    std::string label;                // Display label: "Sikran", "Trash #3", "Pull 1"
    uint32_t pullNumber = 0;          // Sequential pull number in this session

    // Encounter data (if this is a boss pull)
    bool isEncounter = false;         // True if bounded by ENCOUNTER_START/END
    uint32_t encounterId = 0;         // Boss encounter ID (0 if trash)
    uint32_t difficultyId = 0;        // Difficulty ID (0 if unknown)
    bool success = false;             // True if boss was killed

    // M+ dungeon context
    bool inMythicPlus = false;        // True if within CHALLENGE_MODE_START/END
    uint32_t keystoneLevel = 0;       // M+ keystone level (0 if not M+)
    uint32_t dungeonRunId = 0;        // Which dungeon/encounter run this pull belongs to

    // Group header info, carried on every segment of an M+ run so the
    // segment selector can draw one collapsible group per dungeonRunId
    // without a separate lookup. Empty on standalone raid boss pulls.
    std::string dungeonName;          // Zone name of the run this belongs to
    int32_t dungeonStartTime_ms = 0;  // Run start (CHALLENGE_MODE_START) time
    int32_t dungeonEndTime_ms = 0;    // Run end time; 0 while the run is still live

    // Segment classification
    PullSegmentType segmentType = PullSegmentType::Unknown;
    uint32_t trashNumber = 0;         // Trash #N within dungeon run
    uint32_t bossNumber = 0;          // Boss #N within dungeon run

    // Calculate pull duration
    int32_t getDurationMs() const {
        if (endTime_ms <= 0) return 0;  // Pull still in progress
        return endTime_ms - startTime_ms;
    }

    // Format duration as "M:SS" string
    std::string getDurationString() const {
        int32_t duration = getDurationMs();
        if (duration <= 0) return "...";
        int seconds = (duration / 1000) % 60;
        int minutes = duration / 60000;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d", minutes, seconds);
        return buf;
    }

    // Check if pull is still in progress (no end event yet)
    bool isInProgress() const {
        return endTime_ms <= 0 || endByteOffset == 0;
    }
};

// Combat state for pull detection
enum class CombatState {
    Idle,           // No combat - waiting for first damage event
    InCombat,       // Combat in progress
    InEncounter     // Inside ENCOUNTER_START/END boundaries
};

// View state for historical/live mode switching in overlay
enum class SessionViewState {
    Live,           // Normal live polling mode - data accumulates
    Historical,     // Viewing a single historical segment (re-parsed from byte offsets)
    Overall         // Viewing aggregated dungeon data (all segments re-parsed)
};
