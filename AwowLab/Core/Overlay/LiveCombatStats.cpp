#include "LiveCombatStats.h"

LiveCombatStats::LiveCombatStats() {
    combatDb_ = std::make_unique<CombatDatabase>();
    dispelDb_ = std::make_unique<DispelDatabase>();
    deathDb_ = std::make_unique<DeathDatabase>();
    resourceDb_ = std::make_unique<ResourceDatabase>();
    auraDb_ = std::make_unique<AuraDatabase>();
    absorbDb_ = std::make_unique<AbsorbDatabase>();
    avoidanceDb_ = std::make_unique<AvoidanceDatabase>();
}

LiveCombatStats::~LiveCombatStats() = default;

void LiveCombatStats::attachSession(LiveLogSession* session) {
    session_ = session;
    sessionStartTime_ms_ = 0;
    dungeonStartTime_ms_ = 0;
    selectedPullIndex_ = SIZE_MAX;
    refresh();
}

void LiveCombatStats::detachSession() {
    session_ = nullptr;
    combatDb_ = std::make_unique<CombatDatabase>();
    dispelDb_ = std::make_unique<DispelDatabase>();
    deathDb_ = std::make_unique<DeathDatabase>();
    resourceDb_ = std::make_unique<ResourceDatabase>();
    auraDb_ = std::make_unique<AuraDatabase>();
    absorbDb_ = std::make_unique<AbsorbDatabase>();
    avoidanceDb_ = std::make_unique<AvoidanceDatabase>();
    selectedPullIndex_ = SIZE_MAX;
}

void LiveCombatStats::refresh() {
    if (!session_) return;

    // Rebuild every database from the session's DISPLAYED bundle - the
    // live combat data while viewing Current, or the re-parsed historical
    // bundle while a past segment / M+ Overall is open. getActorMap and
    // the event-stream getters resolve to whichever bundle the view state
    // selects, so this same code path serves both live and history.
    //
    // Hand the combat db the SPELL_SUMMON lineage so a player's summoned
    // Creature-/Vehicle- pets (a Death Knight's army, etc.) merge into the
    // player even when their damage events carry no owner in the advanced
    // combat log block.
    auto summonPetToOwner = session_->getSummonPetToOwnerMap();
    combatDb_->loadFromActorMap(&session_->getActorMap(), &summonPetToOwner);

    // ResourceDatabase is a facade over the same ActorMap - it just
    // needs its pointer refreshed. HP timeline for the death recap
    // pulls from resource_status_table which LiveLogSession appends
    // to as it parses damage/heal events with advanced logging.
    resourceDb_->loadFromActorMap(&session_->getActorMap());

    // Rebuild event-backed databases from the streams the session
    // accumulates during processLines. We copy (not move) because the
    // session's vectors keep growing over the whole session lifetime;
    // the copy overhead is small compared to the ranking queries and
    // matches how the main app's DatabaseFacade behaves.
    absorbDb_->loadFromEvents(session_->getAbsorbEvents());
    dispelDb_->loadFromEvents(session_->getDispelEvents());
    avoidanceDb_->loadFromEvents(session_->getMissedEvents());

    // DeathDatabase takes deaths + resurrects. We don't wire resurrects
    // into the live session yet (SPELL_RESURRECT parsing not added), so
    // pass an empty vector. Position-inference for implicit resurrects
    // is only relevant for M+ historical view.
    static const std::vector<ResurrectEvent> kEmptyResurrects;
    deathDb_->loadEvents(session_->getDeathEvents(), kEmptyResurrects);

    // AuraDatabase::loadFromEvents wants a mutable rvalue - copy the
    // session's vector into a local so the session retains its own copy
    // for future refreshes.
    std::vector<AuraEvent> auraCopy = session_->getAuraEvents();
    auraDb_->loadFromEvents(std::move(auraCopy));
}

std::vector<ActorCombatStats> LiveCombatStats::getDamageRanked(
    StatsViewMode mode,
    size_t limit
) const {
    if (!combatDb_ || combatDb_->empty()) {
        return {};
    }

    auto [startTime, endTime] = getTimeRange(mode);

    return combatDb_->getRankedByActorWithPets(
        CombatMetricType::DamageDealt,
        startTime,
        endTime,
        limit,
        false  // No spell breakdowns for overlay (too expensive)
    );
}

std::vector<ActorCombatStats> LiveCombatStats::getHealingRanked(
    StatsViewMode mode,
    size_t limit
) const {
    if (!combatDb_ || combatDb_->empty()) {
        return {};
    }

    auto [startTime, endTime] = getTimeRange(mode);

    return combatDb_->getRankedByActorWithPets(
        CombatMetricType::HealingDone,
        startTime,
        endTime,
        limit,
        false
    );
}

std::vector<ActorCombatStats> LiveCombatStats::getDamageTakenRanked(
    StatsViewMode mode,
    size_t limit
) const {
    if (!combatDb_ || combatDb_->empty()) {
        return {};
    }

    auto [startTime, endTime] = getTimeRange(mode);

    return combatDb_->getRankedByDamageTakenWithPets(
        startTime,
        endTime,
        limit,
        false
    );
}

std::vector<ActorDispelStats> LiveCombatStats::getInterruptsRanked(
    StatsViewMode mode,
    size_t limit
) const {
    if (!dispelDb_ || dispelDb_->empty()) {
        return {};
    }

    auto [startTime, endTime] = getTimeRange(mode);

    return dispelDb_->getRankedByActor(
        startTime,
        endTime,
        limit,
        false,
        DispelDatabase::FilterType::InterruptsOnly
    );
}

std::vector<ActorDispelStats> LiveCombatStats::getDispelsRanked(
    StatsViewMode mode,
    size_t limit
) const {
    if (!dispelDb_ || dispelDb_->empty()) {
        return {};
    }

    auto [startTime, endTime] = getTimeRange(mode);

    return dispelDb_->getRankedByActor(
        startTime,
        endTime,
        limit,
        false,
        DispelDatabase::FilterType::DispelsOnly
    );
}

void LiveCombatStats::launchAsyncSelect(std::function<void()> work) {
    // Detach a worker thread that runs the parse/refresh chain off
    // the UI thread. selectMutex_ serializes concurrent selects so a
    // fast click through several segments does not race two parses
    // into the same ActorMap. selectInFlight_ is a cheap "already
    // queued" check to avoid spawning a stack of threads if the user
    // spam-clicks a picker. LiveLogSession::parseSegment /
    // parseSegments already flip parsingInProgress_ for the duration
    // of their work, which the overlay meter panel polls to drive
    // the spinner.
    if (selectInFlight_.exchange(true)) {
        // Someone else already has the queue; drop this select. The
        // in-flight worker will exit and future clicks will pick up
        // the freshest selectedPullIndex_.
        return;
    }
    std::thread([this, w = std::move(work)]() mutable {
        std::lock_guard<std::mutex> guard(selectMutex_);
        // Extend the session's parsingInProgress_ signal across the
        // whole worker so it covers parseSegment AND the follow-up
        // refresh() that rebuilds CombatDatabase / ResourceDatabase
        // / etc. Without this the render path could read a
        // half-rebuilt petToOwnerMap_ between the moment
        // parseSegment released the flag and refresh() started
        // clearing it - which manifested as intermittent access
        // violations inside unordered_map::find on the UI thread.
        if (session_) session_->beginExternalParse();
        w();
        if (session_) session_->endExternalParse();
        selectInFlight_.store(false);
    }).detach();
}

void LiveCombatStats::selectHistoricalPull(size_t pullIndex) {
    // Store index instead of pointer to avoid invalidation when
    // vector grows. Set immediately (before the parse actually runs)
    // so getTimeRange() and other readers see the new selection while
    // the background worker is still crunching.
    selectedPullIndex_ = pullIndex;
    if (!session_) return;

    launchAsyncSelect([this, pullIndex]() {
        if (!session_) return;
        auto snapshot = session_->getSnapshot();
        if (pullIndex >= snapshot.pullHistory.size()) return;
        const auto& segment = snapshot.pullHistory[pullIndex];
        if (session_->parseSegment(segment)) {
            refresh();  // Rebuild databases from new ActorMap
        }
    });
}

void LiveCombatStats::selectOverallView() {
    if (!session_) return;

    launchAsyncSelect([this]() {
        if (!session_) return;
        auto snapshot = session_->getSnapshot();
        uint32_t currentRunId = session_->getCurrentDungeonRunId();

        // Collect all segments in current dungeon
        std::vector<PullSegment> segments;
        for (const auto& pull : snapshot.pullHistory) {
            if (pull.dungeonRunId == currentRunId) {
                segments.push_back(pull);
            }
        }

        // Include current pull if it belongs to this dungeon
        if (snapshot.currentPull.dungeonRunId == currentRunId &&
            snapshot.currentPull.startByteOffset > 0) {
            // For in-progress pulls, use current file position as end
            PullSegment current = snapshot.currentPull;
            if (current.endByteOffset == 0) {
                current.endByteOffset = session_->getCurrentFileSize();
            }
            segments.push_back(current);
        }

        if (!segments.empty()) {
            if (session_->parseSegments(segments)) {
                refresh();  // Rebuild databases from aggregated ActorMap
            }
        }
    });
}

void LiveCombatStats::clearHistoricalPullSelection() {
    selectedPullIndex_ = SIZE_MAX;

    // Return to live mode
    if (session_) {
        session_->returnToLiveMode();
        refresh();
    }
}

std::pair<int32_t, int32_t> LiveCombatStats::getTimeRange(StatsViewMode mode) const {
    if (!session_) {
        return {0, INT32_MAX};
    }

    // Get thread-safe snapshot for accessing pull data
    auto snapshot = session_->getSnapshot();

    switch (mode) {
        case StatsViewMode::CurrentPull: {
            // The top "live" option is a sliding view over the currently
            // active combat segment:
            //
            //  - Between bosses, currentPull_ is the collated trash
            //    segment (one per inter-boss gap now that idle no longer
            //    splits it). Its window is [trashStart, now], growing as
            //    each trash flush lands.
            //
            //  - During a boss, currentPull_ is the boss segment. Boss
            //    records don't appear in the log until the kill flushes
            //    them, so there's a stretch after ENCOUNTER_START where
            //    the boss window holds no data yet. For that stretch we
            //    fall back to session-so-far (min..max over everything
            //    the combatDb currently holds - the preceding trash,
            //    which startNewPull deliberately left in place) so the
            //    meter shows the run rather than blanking. The instant
            //    the boss's own records land we snap to just the boss
            //    window.
            const PullSegment* pull = &snapshot.currentPull;

            // Don't flip to a brand-new segment for the first few seconds.
            // Every boss pull and trash block opens a fresh segment, and
            // snapping the live meter to it the instant it starts makes the
            // numbers blink to near-empty. While the active segment is
            // younger than this, keep showing the previous one; the switch
            // happens once the new segment has some substance.
            constexpr int32_t kMinSegmentAgeMs = 3000;
            if (combatDb_ && !snapshot.pullHistory.empty()) {
                int32_t segAge = combatDb_->getMaxTimestamp() - pull->startTime_ms;
                if (segAge >= 0 && segAge < kMinSegmentAgeMs) {
                    return getPullTimeRange(&snapshot.pullHistory.back());
                }
            }

            if (pull->isEncounter && combatDb_) {
                // "Has the boss data flushed?" Parsing is timestamp
                // ordered, so the combatDb's max timestamp only reaches
                // the boss's [start,end] window once the kill burst has
                // been parsed. Before that, max sits at the pre-boss
                // trash tail (< the boss start), so this is false and we
                // use the session fallback below. A cheap, robust check
                // that needs no per-record scan.
                bool bossDataFlushed =
                    combatDb_->getMaxTimestamp() >= pull->startTime_ms;
                if (!bossDataFlushed) {
                    // Session-so-far: everything currently loaded. An
                    // empty combatDb reports min as UINT32_MAX (-1 as
                    // int32) and max as 0, so guard on empty() and hand
                    // back a wide-open window rather than that sentinel
                    // pair.
                    if (combatDb_->empty()) {
                        return {0, INT32_MAX};
                    }
                    return {combatDb_->getMinTimestamp(),
                            combatDb_->getMaxTimestamp()};
                }
                // Boss data present - snap to exactly the boss window.
                // getPullTimeRange keeps the in-progress end at INT32_MAX
                // so late interrupts/deaths just past the last damage
                // event aren't truncated (each render clamps further).
            }

            return getPullTimeRange(pull);
        }

        case StatsViewMode::HistoricalPull: {
            // parseSegment rebases event timestamps to start at 0 for
            // the segment (see LiveLogSession::parseSegment). The
            // ranking window has to match: [0, duration], not the
            // log-relative [pull.startTime_ms, pull.endTime_ms] baked
            // into the PullSegment metadata. Using the raw metadata
            // would filter to a window ~2h into the raid while the
            // records live at ~0-5min, and every meter would come
            // back empty ("no damage data available"). Only the
            // HistoricalPull case is affected; CurrentPull stays on
            // log-relative timestamps from live poll().
            if (selectedPullIndex_ != SIZE_MAX && selectedPullIndex_ < snapshot.pullHistory.size()) {
                const auto& pull = snapshot.pullHistory[selectedPullIndex_];
                int32_t duration = pull.getDurationMs();
                if (duration <= 0) {
                    return {0, 0};
                }
                return {0, duration};
            }
            // Fall back to current pull if no historical selected
            return getPullTimeRange(&snapshot.currentPull);
        }

        case StatsViewMode::CurrentDungeon: {
            // From dungeon start to now
            int32_t startTime = session_ ? session_->getDungeonStartTime() : 0;
            int32_t endTime = combatDb_ ? combatDb_->getMaxTimestamp() : INT32_MAX;
            return {startTime, endTime};
        }

        case StatsViewMode::TrashOverall: {
            // Aggregate all trash pulls in current dungeon
            auto ranges = getTrashPullTimeRanges();
            if (ranges.empty()) {
                return {0, 0};
            }
            // Return min start to max end for stats calculation
            int32_t minStart = ranges.front().first;
            int32_t maxEnd = ranges.front().second;
            for (const auto& [start, end] : ranges) {
                minStart = std::min(minStart, start);
                maxEnd = std::max(maxEnd, end);
            }
            return {minStart, maxEnd};
        }

        case StatsViewMode::SessionTotal: {
            // All data
            int32_t startTime = combatDb_ ? combatDb_->getMinTimestamp() : 0;
            int32_t endTime = combatDb_ ? combatDb_->getMaxTimestamp() : INT32_MAX;
            return {startTime, endTime};
        }
    }

    return {0, INT32_MAX};
}

std::pair<int32_t, int32_t> LiveCombatStats::getPullTimeRange(const PullSegment* pull) const {
    if (!pull) {
        return {0, INT32_MAX};
    }

    int32_t startTime = pull->startTime_ms;
    // For in-progress pulls we intentionally return INT32_MAX rather
    // than combatDb->getMaxTimestamp(). Interrupts, dispels, deaths
    // and CC breaks live in their own databases and often carry
    // timestamps slightly beyond the last damage/heal event; clamping
    // to combatDb's max would silently truncate them out of the
    // Interrupts/Deaths/etc meters. Each render function calls
    // calculateTimeWindow which further clamps to the meter's own DB
    // bounds, so INT32_MAX is safe here.
    int32_t endTime = pull->isInProgress()
        ? INT32_MAX
        : pull->endTime_ms;

    return {startTime, endTime};
}

float LiveCombatStats::getDurationSeconds(StatsViewMode mode) const {
    auto [startTime, endTime] = getTimeRange(mode);
    if (endTime <= startTime) return 0.0f;
    return static_cast<float>(endTime - startTime) / 1000.0f;
}

std::string LiveCombatStats::getCurrentSegmentName() const {
    if (!session_) return "";

    auto snapshot = session_->getSnapshot();

    if (selectedPullIndex_ != SIZE_MAX && selectedPullIndex_ < snapshot.pullHistory.size()) {
        return snapshot.pullHistory[selectedPullIndex_].label;
    }

    // The live segment's label is currentPull_'s: the collated trash
    // block between bosses, the boss name during/after a boss. During the
    // session-so-far fallback window (boss ENCOUNTER_START seen but its
    // records not flushed yet) currentPull_ is already the boss, so the
    // header reads the boss name - that's what's being fought, which is
    // the least surprising label even while the meter is showing the
    // wider session window underneath.
    return snapshot.currentPull.label;
}

bool LiveCombatStats::isInEncounter() const {
    if (!session_) return false;
    auto snapshot = session_->getSnapshot();
    return snapshot.inEncounter;
}

bool LiveCombatStats::isInMythicPlus() const {
    if (!session_) return false;
    auto snapshot = session_->getSnapshot();
    return snapshot.inMythicPlus;
}

const PullSegment* LiveCombatStats::getCurrentPull() const {
    // NOTE: This returns a pointer to internal snapshot data.
    // The caller must not store this pointer - it becomes invalid on next getSnapshot() call.
    // Consider using getSnapshot() directly instead.
    if (!session_) return nullptr;
    // This is unsafe - returning pointer to local. Caller should use getSnapshot() instead.
    // For now, return nullptr and let callers migrate to snapshot-based access.
    return nullptr;
}

const std::vector<PullSegment>* LiveCombatStats::getPullHistory() const {
    // NOTE: This is unsafe - vector may be modified by background thread.
    // Callers should use session_->getSnapshot().pullHistory instead.
    if (!session_) return nullptr;
    return nullptr;  // Force callers to use snapshot
}

std::vector<std::pair<int32_t, int32_t>> LiveCombatStats::getTrashPullTimeRanges() const {
    std::vector<std::pair<int32_t, int32_t>> ranges;
    if (!session_) return ranges;

    auto snapshot = session_->getSnapshot();
    uint32_t currentRunId = session_->getCurrentDungeonRunId();

    // Collect all trash pulls from history in current dungeon
    for (const auto& pull : snapshot.pullHistory) {
        if (pull.dungeonRunId == currentRunId &&
            pull.segmentType == PullSegmentType::TrashPull) {
            ranges.emplace_back(pull.startTime_ms, pull.endTime_ms);
        }
    }

    // Include current pull if it's trash and in current dungeon
    if (snapshot.currentPull.segmentType == PullSegmentType::TrashPull &&
        snapshot.currentPull.dungeonRunId == currentRunId) {
        int32_t endTime = snapshot.currentPull.isInProgress()
            ? (combatDb_ ? combatDb_->getMaxTimestamp() : INT32_MAX)
            : snapshot.currentPull.endTime_ms;
        ranges.emplace_back(snapshot.currentPull.startTime_ms, endTime);
    }

    return ranges;
}
