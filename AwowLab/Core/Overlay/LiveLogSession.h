#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <chrono>
#include <functional>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <atomic>

#include "PullSegment.h"
#include "Parser/Include/Actor/ActorMap.h"
#include "Parser/Include/Encounter/EncounterSegment.h"
#include "Database/EventTypes.h"
#include "Database/AuraDatabase.h"   // for AuraEvent

class CombatDatabase;
class DispelDatabase;
class ResourceDatabase;

// First completed cast of a spell within the loaded data window
// (current live pull, or the re-parsed historical segment). Phase
// rules of the "first cast of X" kind resolve against these, and the
// phase editor lists the hostile ones as candidate split points.
struct LiveFirstCast {
    int32_t time_ms = 0;
    std::string spell_name;
    bool hostile_source = false;  // boss/add cast (the interesting kind)
};

// Manages incremental parsing of a growing WoW combat log file.
// Designed for live overlay mode where the log file is being written by WoW.
//
// Key features:
// - Reads only new bytes since last parse (tracks lastParsedOffset_)
// - Handles partial lines at EOF (WoW may write incomplete lines)
// - Accumulates combat data into ActorMap for stat queries
// - Tracks pull/encounter boundaries with cached byte offsets
// - Supports navigation to historical pulls via byte offset re-parsing
//
// Usage:
//   LiveLogSession session;
//   session.attach("C:/.../_retail_/Logs/WoWCombatLog-123.txt");
//
//   // Called periodically when ActiveLogWatcher signals new data
//   if (session.poll()) {
//       // New events were parsed - update UI
//   }
//
class LiveLogSession {
public:
    // Thread-safe snapshot of current session state for UI consumption.
    // Copied atomically at the end of each poll() to avoid iterator invalidation.
    //
    // Design: Only snapshots current pull data. Historical pulls use cached byte
    // offsets in PullSegment for on-demand re-parsing - no need to copy history.
    struct Snapshot {
        // Name lookups (needed for display)
        std::unordered_map<std::string, std::string> guidToName;
        std::unordered_map<uint32_t, std::string> spellIdToName;

        // Current pull state
        PullSegment currentPull;
        bool inCombat = false;
        bool inEncounter = false;
        bool inMythicPlus = false;

        // Pull history metadata (lightweight - just labels/offsets/times, no combat data)
        // This is safe to copy since PullSegment is small (~80 bytes each)
        std::vector<PullSegment> pullHistory;

        // Phase-rule inputs for the loaded data window: first cast per
        // spell id and the emote lines seen. Cleared with the rest of
        // the pull data, so they always describe whatever segment the
        // meters are showing. Both stay small (distinct spell ids per
        // pull; emotes capped at kMaxEmotesPerSegment).
        std::unordered_map<uint32_t, LiveFirstCast> firstCasts;
        std::vector<EmoteEvent> emotes;

        // Player spec ids from COMBATANT_INFO (fires at every
        // ENCOUNTER_START / M+ start). Session-lifetime like the name
        // lookups; drives class-colored meter bars.
        std::unordered_map<std::string, uint16_t> guidToSpecId;
    };

    // Callbacks for UI updates
    using PullStartCallback = std::function<void(const PullSegment&)>;
    using PullEndCallback = std::function<void(const PullSegment&)>;
    using DataUpdateCallback = std::function<void()>;
    using DungeonStartCallback = std::function<void(uint32_t runId, const std::string& name, uint32_t level)>;

    LiveLogSession();
    ~LiveLogSession();

    // Non-copyable
    LiveLogSession(const LiveLogSession&) = delete;
    LiveLogSession& operator=(const LiveLogSession&) = delete;

    // Attach to an active combat log file
    // Returns true if the file was opened successfully
    // If scanExisting is true, scans for segment boundaries only (fast startup)
    bool attach(const std::filesystem::path& logPath, bool scanExisting = false);

    // Scan file for segment boundaries without parsing combat data
    // This is a lightweight scan that only extracts byte offsets and metadata
    // for ENCOUNTER_START/END, CHALLENGE_MODE_START/END events
    void scanForSegments();

    // Detach from the current file
    void detach();

    // Check if attached to a file
    bool isAttached() const { return !logPath_.empty(); }

    // Get the attached file path
    const std::filesystem::path& getLogPath() const { return logPath_; }

    // Poll for new data - reads and parses any new bytes in the file
    // Returns true if new events were parsed
    bool poll();

    // === Pull History Navigation ===

    // Get all detected pulls (for history dropdown)
    const std::vector<PullSegment>& getPullHistory() const { return pullHistory_; }

    // Get the currently selected pull (nullptr = live/current)
    const PullSegment* getSelectedPull() const { return selectedPull_; }

    // Select a historical pull by index (re-parses that segment)
    void selectPull(size_t pullIndex);

    // Return to live/current pull tracking
    void selectCurrentPull();

    // === Segment Re-Parsing (for historical view) ===

    // Parse a specific segment from file using byte offsets
    // Clears current ActorMap and rebuilds from segment data only
    // Returns true if parsing succeeded
    bool parseSegment(const PullSegment& segment);

    // Parse multiple segments (for "Overall" aggregation)
    // Clears ActorMap first, then accumulates data from all segments
    bool parseSegments(const std::vector<PullSegment>& segments);

    // Return to live mode (resume normal polling)
    // Clears historical data and continues from current file position
    void returnToLiveMode();

    // Get current view state
    SessionViewState getViewState() const { return viewState_; }

    // Get the current pull (may be in progress)
    const PullSegment& getCurrentPull() const { return currentPull_; }

    // === Thread-Safe Data Access ===

    // Get a thread-safe snapshot of all session data.
    // Call this once per frame from the UI thread and use the returned data.
    // The snapshot is updated atomically at the end of each poll().
    Snapshot getSnapshot() const;

    // Check if parsing is currently in progress (ActorMap being modified)
    // If true, UI should skip rendering this frame to avoid iterator invalidation
    bool isParsingInProgress() const { return parsingInProgress_.load(); }

    // True while the initial whole-log segment scan is running. Distinct
    // from isParsingInProgress so the UI can say "scanning the log"
    // (with segments streaming into the dropdown) rather than the
    // generic parsing spinner.
    bool isScanningSegments() const { return segmentScanInProgress_.load(); }

    // Manually raise/lower the parsing-in-progress signal from
    // external code. Used by LiveCombatStats::launchAsyncSelect to
    // keep the flag held across BOTH the parseSegment call (which
    // toggles it internally) and the follow-up refresh() that
    // rebuilds CombatDatabase et al. Without this the render path
    // would happily read a half-swapped petToOwnerMap_ between the
    // moment parseSegment released the flag and refresh() started
    // rebuilding, causing intermittent access-violation crashes on
    // unordered_map::find.
    void beginExternalParse() { parsingInProgress_.store(true); }
    void endExternalParse()   { parsingInProgress_.store(false); }

    // Try to acquire a lock on ActorMap for safe iteration.
    // Waits briefly (single-digit ms) so an in-flight parse slice can
    // finish; returns a unique_lock that may not own the lock if a
    // long-running parse still holds it.
    std::unique_lock<std::timed_mutex> tryLockActorMap() const;

    // Blocking version for non-render threads that need to mutate data
    // the render path reads (e.g. rebuilding the stat databases after a
    // poll). Never call from the render thread.
    std::unique_lock<std::timed_mutex> lockActorMap() const;

    // === Legacy Data Access (NOT thread-safe, use getSnapshot() instead) ===

    // Get the accumulated ActorMap (for database queries)
    const ActorMap& getActorMap() const { return actorMap_; }

    // Absorb events accumulated since attach() or resetAll().
    // Not thread-safe with concurrent poll(); use tryLockActorMap() to gate iteration.
    const std::vector<AbsorbEvent>& getAbsorbEvents() const { return absorbEvents_; }

    // Additional per-event streams the meter panels consume. Same
    // threading rules as getAbsorbEvents. Cleared on attach/resetAll.
    const std::vector<MissedEvent>& getMissedEvents() const { return missedEvents_; }
    const std::vector<DispelInterruptEvent>& getDispelEvents() const { return dispelEvents_; }
    const std::vector<DeathEvent>& getDeathEvents() const { return deathEvents_; }
    const std::vector<AuraEvent>& getAuraEvents() const { return auraEvents_; }

    // Phase-rule inputs (same threading rules; prefer the snapshot
    // copies from the render thread)
    const std::unordered_map<uint32_t, LiveFirstCast>& getFirstCasts() const { return firstCasts_; }
    const std::vector<EmoteEvent>& getEmoteEvents() const { return emoteEvents_; }
    const std::unordered_map<std::string, uint16_t>& getGuidToSpecIdMap() const { return guidToSpecId_; }

    // Get name lookup map (GUID -> name)
    const std::unordered_map<std::string, std::string>& getGuidToNameMap() const { return guidToName_; }

    // Get spell name lookup map (spell_id -> name)
    const std::unordered_map<uint32_t, std::string>& getSpellNameMap() const { return spellIdToName_; }

    // === Stats ===

    // Get parsing statistics
    size_t getLastParsedOffset() const { return lastParsedOffset_; }
    size_t getCurrentFileSize() const;
    std::chrono::steady_clock::time_point getLastParseTime() const { return lastParseTime_; }

    // Get encounter info
    const std::vector<EncounterSegment>& getEncounters() const { return encounters_; }
    bool isInEncounter() const { return combatState_ == CombatState::InEncounter; }
    bool isInCombat() const { return combatState_ != CombatState::Idle; }

    // === Session Management ===

    // Reset all accumulated data (start fresh)
    void resetAll();

    // Reset only the current pull data
    void resetCurrentPull();

    // === Callbacks ===

    void setOnPullStart(PullStartCallback callback) { onPullStart_ = std::move(callback); }
    void setOnPullEnd(PullEndCallback callback) { onPullEnd_ = std::move(callback); }
    void setOnDataUpdate(DataUpdateCallback callback) { onDataUpdate_ = std::move(callback); }
    void setOnDungeonStart(DungeonStartCallback callback) { onDungeonStart_ = std::move(callback); }

    // Combat idle timeout for pull detection (default 5 seconds)
    void setCombatIdleTimeout(std::chrono::milliseconds timeout) { combatIdleTimeout_ = timeout; }

    // Dungeon/encounter run tracking
    uint32_t getCurrentDungeonRunId() const { return currentDungeonRunId_; }
    int32_t getDungeonStartTime() const { return dungeonStartTime_ms_; }
    const std::string& getCurrentDungeonName() const { return currentDungeonName_; }

private:
    std::filesystem::path logPath_;
    size_t lastParsedOffset_ = 0;
    std::string pendingPartialLine_;  // Incomplete line from last read
    std::chrono::steady_clock::time_point lastParseTime_;

    // Thread synchronization for UI snapshot access
    mutable std::mutex snapshotMutex_;
    Snapshot snapshot_;  // Thread-safe copy for UI access

    // Accumulated data (modified by background poll thread)
    ActorMap actorMap_;
    std::unordered_map<std::string, std::string> guidToName_;
    std::unordered_map<uint32_t, std::string> spellIdToName_;
    std::vector<EncounterSegment> encounters_;
    std::vector<AbsorbEvent> absorbEvents_;
    std::vector<MissedEvent> missedEvents_;
    std::vector<DispelInterruptEvent> dispelEvents_;
    std::vector<DeathEvent> deathEvents_;
    std::vector<AuraEvent> auraEvents_;  // only Broken/BrokenSpell for now

    // Phase-rule inputs for the loaded data window. firstCasts_ keeps
    // only the earliest completed cast per spell id; emoteEvents_ is
    // capped at kMaxEmotesPerSegment. Both cleared with the rest of
    // the pull data in clearLivePullData().
    static constexpr size_t kMaxEmotesPerSegment = 256;
    std::unordered_map<uint32_t, LiveFirstCast> firstCasts_;
    std::vector<EmoteEvent> emoteEvents_;

    // Player GUID -> spec id from COMBATANT_INFO. Session context like
    // guidToName_ (a player's spec does not stop being true when the
    // pull ends), so it survives pull boundaries and segment replays.
    std::unordered_map<std::string, uint16_t> guidToSpecId_;

    // Pull tracking
    std::vector<PullSegment> pullHistory_;
    PullSegment currentPull_;
    const PullSegment* selectedPull_ = nullptr;
    uint32_t nextPullNumber_ = 1;
    uint32_t currentTrashNumber_ = 0;
    uint32_t currentBossNumber_ = 0;

    // Pet -> owner mapping learned from SPELL_SUMMON events. Combat
    // records emitted by short-lived pets (Wild Imps, Nether Portal
    // demons, etc.) often ship without a populated
    // advanced_info.owner_guid, so the offline OutputWriter uses
    // SPELL_SUMMON as a fallback source of ownership. The live path
    // now does the same. Persists across pulls since summons can
    // outlive the pull that spawned them.
    std::unordered_map<std::string, std::string> petToOwnerFromSummons_;

    // Combat state machine
    CombatState combatState_ = CombatState::Idle;
    int32_t lastCombatEventTime_ms_ = 0;
    int64_t logStartTimestamp_ = 0;  // First timestamp in log (for relative time calc)
    bool logStartTimestampSet_ = false;
    std::chrono::milliseconds combatIdleTimeout_{5000};

    // View state for historical/live mode
    SessionViewState viewState_ = SessionViewState::Live;

    // M+ context
    bool inMythicPlus_ = false;
    uint32_t currentKeystoneLevel_ = 0;

    // Dungeon/encounter run tracking
    uint32_t currentDungeonRunId_ = 0;    // Incremented on each CHALLENGE_MODE_START or first ENCOUNTER_START
    int32_t dungeonStartTime_ms_ = 0;     // Timestamp of current dungeon/encounter start
    std::string currentDungeonName_;       // Zone/encounter name for display

    // Challenge mode tracking for Overall segment creation
    size_t challengeModeStartOffset_ = 0;  // Byte offset of CHALLENGE_MODE_START
    int32_t challengeModeStartTime_ms_ = 0; // Timestamp of CHALLENGE_MODE_START

    // Callbacks
    PullStartCallback onPullStart_;
    PullEndCallback onPullEnd_;
    DataUpdateCallback onDataUpdate_;
    DungeonStartCallback onDungeonStart_;

    // Thread safety for ActorMap access. Timed so the render thread can
    // wait out a short parse slice instead of dropping to a spinner.
    mutable std::timed_mutex actorMapMutex_;
    std::atomic<bool> parsingInProgress_{false};
    std::atomic<bool> segmentScanInProgress_{false};

    // Internal helpers.
    // processLines handles lines [firstLine, firstLine + lineCount) and
    // returns the estimated byte offset after the last processed line,
    // so poll() can feed a big batch through in slices without holding
    // the ActorMap lock for the whole batch.
    size_t processLines(const std::vector<std::vector<std::string_view>>& lines,
                        size_t startByteOffset,
                        size_t firstLine = 0,
                        size_t lineCount = SIZE_MAX);
    void handleCombatEvent(int32_t timestamp_ms, size_t byteOffset);

    // Parse a single combat log line and stash its data into ActorMap
    // and/or the per-event vectors (damage, heal, absorb, miss, dispel,
    // death, aura). Returns true if the event was a combat event that
    // should participate in pull tracking (i.e. damage/heal/absorb/miss/
    // dispel; not deaths/auras/encounter boundaries). No state
    // mutation beyond data storage - callers are responsible for pull
    // tracking, timestamp normalization, encounter boundaries, etc.
    bool parseAndStoreEvent(const std::vector<std::string_view>& tokens,
                            std::string_view eventType,
                            int32_t timestamp_ms);

    // Resolve the effective owner_guid_id for a combat record. Prefers
    // advanced_info.owner_guid when it names a Player, falls back to
    // the SPELL_SUMMON-derived pet->owner map for pets whose advanced
    // block omits the owner (Wild Imps and similar). Returns 0 when no
    // ownership can be established.
    uint32_t resolveOwnerGuidId(std::string_view source_guid,
                                std::string_view advanced_owner_guid);

    // Append a resource-status snapshot (current/max HP + power) to
    // the ActorMap for whichever actor the advanced-info block
    // describes. Called from every damage/heal branch since those are
    // the events that carry advanced_info in the combat log with HP
    // fields. Silently no-ops when info_guid doesn't match source or
    // dest (skips synthetic entries) or when HP is zero.
    void appendResourceSnapshot(std::string_view source_guid,
                                std::string_view dest_guid,
                                const struct AdvancedUnitInfo& info,
                                int32_t timestamp_ms);

    // Clear ActorMap and the per-event vectors. Preserves guidToName_,
    // spellIdToName_, pullHistory_, encounters_ and the dungeon-run
    // bookkeeping - only combat data belongs to the current pull, the
    // rest is session context. Called at every "start of a fresh live
    // pull" boundary (endCurrentPull) and at every historical
    // re-parse boundary (parseSegment / parseSegments / return to
    // live). Not called from scanForSegments which never writes combat
    // data.
    void clearLivePullData();
    void handleEncounterStart(const std::vector<std::string_view>& tokens, size_t byteOffset);
    void handleEncounterEnd(const std::vector<std::string_view>& tokens, size_t byteOffset);
    void handleChallengeModeStart(const std::vector<std::string_view>& tokens);
    void handleChallengeModeEnd(const std::vector<std::string_view>& tokens);
    void startNewPull(int32_t timestamp_ms, size_t byteOffset, const std::string& label);
    void endCurrentPull(int32_t timestamp_ms, size_t byteOffset);
    void checkIdleTimeout(int32_t currentTime_ms, size_t byteOffset);
    // Date-aware timestamp conversion (epoch milliseconds). Must stay
    // date-aware: a time-of-day-only value makes every pull spanning
    // midnight end "before" it started and pushes all post-midnight
    // times negative relative to the log start.
    int64_t parseTimestampMs(std::string_view date, std::string_view time);
    void updateSnapshot();  // Copy current data to snapshot under lock
};
