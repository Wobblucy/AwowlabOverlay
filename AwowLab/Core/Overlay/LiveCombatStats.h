#pragma once
#include "PullSegment.h"
#include "LiveLogSession.h"
#include "Database/CombatDatabase.h"
#include "Database/DispelDatabase.h"
#include "Database/DeathDatabase.h"
#include "Database/ResourceDatabase.h"
#include "Database/AuraDatabase.h"
#include "Database/AbsorbDatabase.h"
#include "Database/AvoidanceDatabase.h"
#include <memory>
#include <vector>
#include <chrono>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

// View modes for stats aggregation
enum class StatsViewMode {
    CurrentPull,      // Stats for current/latest pull only
    HistoricalPull,   // Stats for a selected historical pull
    CurrentDungeon,   // Cumulative stats since CHALLENGE_MODE_START
    TrashOverall,     // Aggregate all trash pulls in current dungeon
    SessionTotal      // All stats since overlay started
};

// Provides stats aggregation over the live log session with different view modes.
// Acts as a facade over the LiveLogSession's ActorMap, providing time-windowed queries
// based on the selected view mode.
//
// Usage:
//   LiveCombatStats stats;
//   stats.attachSession(&session);
//
//   // Get DPS rankings for current pull
//   auto damage = stats.getDamageRanked(StatsViewMode::CurrentPull, 20);
//
class LiveCombatStats {
public:
    LiveCombatStats();
    ~LiveCombatStats();

    // Attach to a live log session (non-owning pointer)
    void attachSession(LiveLogSession* session);

    // Detach from session
    void detachSession();

    // Rebuild internal databases (call after session data updates)
    void refresh();

    // === Ranked Stats Queries ===

    // Get damage rankings (DPS)
    std::vector<ActorCombatStats> getDamageRanked(
        StatsViewMode mode,
        size_t limit = 20
    ) const;

    // Get healing rankings (HPS)
    std::vector<ActorCombatStats> getHealingRanked(
        StatsViewMode mode,
        size_t limit = 20
    ) const;

    // Get damage taken rankings (DTPS)
    std::vector<ActorCombatStats> getDamageTakenRanked(
        StatsViewMode mode,
        size_t limit = 20
    ) const;

    // Get interrupt rankings
    std::vector<ActorDispelStats> getInterruptsRanked(
        StatsViewMode mode,
        size_t limit = 20
    ) const;

    // Get dispel rankings
    std::vector<ActorDispelStats> getDispelsRanked(
        StatsViewMode mode,
        size_t limit = 20
    ) const;

    // === Historical Pull Selection ===

    // Select a historical pull by index for HistoricalPull mode
    // This triggers re-parsing of the segment from file byte offsets
    void selectHistoricalPull(size_t pullIndex);

    // Select "Overall" view - aggregates all pulls in current dungeon
    // This re-parses all segments and combines their data
    void selectOverallView();

    // Clear historical pull selection (return to CurrentPull/live mode)
    void clearHistoricalPullSelection();

    // Get the currently selected historical pull index (SIZE_MAX if none)
    size_t getSelectedHistoricalPullIndex() const { return selectedPullIndex_; }

    // Check if a historical pull is selected
    bool hasHistoricalPullSelected() const { return selectedPullIndex_ != SIZE_MAX; }

    // === Time Range Info ===

    // Get the time range for a given view mode
    // Returns (start_ms, end_ms) relative to log start
    std::pair<int32_t, int32_t> getTimeRange(StatsViewMode mode) const;

    // Get duration in seconds for a given view mode
    float getDurationSeconds(StatsViewMode mode) const;

    // Get encounter/dungeon name for display
    std::string getCurrentSegmentName() const;

    // === Session Context ===

    // Check if currently in an encounter
    bool isInEncounter() const;

    // Check if currently in M+ dungeon
    bool isInMythicPlus() const;

    // Get current pull info
    const PullSegment* getCurrentPull() const;

    // Get pull history for dropdown
    const std::vector<PullSegment>* getPullHistory() const;

    // === Database Access (for UIMeterPanel integration) ===

    // Get the internal CombatDatabase (for UIMeterPanel)
    const CombatDatabase* getCombatDatabase() const { return combatDb_.get(); }

    // Get the internal DispelDatabase (for UIMeterPanel)
    const DispelDatabase* getDispelDatabase() const { return dispelDb_.get(); }

    // Get the internal DeathDatabase (for UIMeterPanel death view)
    const DeathDatabase* getDeathDatabase() const { return deathDb_.get(); }

    // Get the internal ResourceDatabase (for UIMeterPanel death recap)
    const ResourceDatabase* getResourceDatabase() const { return resourceDb_.get(); }

    // Get the internal AuraDatabase (for UIMeterPanel CC breaks view)
    const AuraDatabase* getAuraDatabase() const { return auraDb_.get(); }

    // Get the internal AbsorbDatabase (for UIMeterPanel Absorbs view)
    const AbsorbDatabase* getAbsorbDatabase() const { return absorbDb_.get(); }

    // Get the internal AvoidanceDatabase (for UIMeterPanel Avoidance view)
    const AvoidanceDatabase* getAvoidanceDatabase() const { return avoidanceDb_.get(); }

private:
    LiveLogSession* session_ = nullptr;

    // Internal databases (rebuilt on refresh)
    std::unique_ptr<CombatDatabase> combatDb_;
    std::unique_ptr<DispelDatabase> dispelDb_;
    std::unique_ptr<DeathDatabase> deathDb_;
    std::unique_ptr<ResourceDatabase> resourceDb_;
    std::unique_ptr<AuraDatabase> auraDb_;
    std::unique_ptr<AbsorbDatabase> absorbDb_;
    std::unique_ptr<AvoidanceDatabase> avoidanceDb_;

    // Selected historical pull index (for HistoricalPull mode)
    // Using index instead of pointer to avoid invalidation when vector grows
    size_t selectedPullIndex_ = SIZE_MAX;

    // Session-level timestamps
    int32_t dungeonStartTime_ms_ = 0;  // Set on CHALLENGE_MODE_START
    int32_t sessionStartTime_ms_ = 0;  // Set when session starts

    // Background segment-parse plumbing. selectHistoricalPull /
    // selectOverallView used to block the UI thread while the file
    // seek + tokenize + parseAndStoreEvent chain ran; on a full boss
    // segment that could freeze the overlay for a couple of seconds.
    // We now spawn a detached worker and rely on
    // LiveLogSession::parsingInProgress_ (already flipped inside
    // parseSegment / parseSegments) to drive the spinner in the
    // meter panel. selectMutex_ serializes overlapping selects so a
    // fast click through several segments does not race two parses
    // into the same ActorMap.
    std::mutex selectMutex_;
    std::atomic<bool> selectInFlight_{false};

    // Run a parseSegment/parseSegments + refresh combination on a
    // detached worker thread. work() gets called under selectMutex_
    // to serialize any queued follow-up selects.
    void launchAsyncSelect(std::function<void()> work);

    // Helper to get time range for current/historical pull
    std::pair<int32_t, int32_t> getPullTimeRange(const PullSegment* pull) const;

    // Helper to get all trash pull time ranges in current dungeon
    std::vector<std::pair<int32_t, int32_t>> getTrashPullTimeRanges() const;
};
