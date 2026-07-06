#include "LiveLogSession.h"
#include "Txttokenizer/Txttokenizer.h"
#include "Parser/Parser.h"
#include "Parser/Parser_Damage.h"
#include "Parser/Parser_Heal.h"
#include "Parser/Parser_Encounter.h"
#include "Parser/Parser_ChallengeMode.h"
#include "Parser/Parser_Absorb.h"
#include "Parser/Parser_Missed.h"
#include "Parser/Parser_Dispel.h"
#include "Parser/Parser_Unit.h"
#include "Parser/Parser_Aura.h"
#include "Parser/Parser_Cast.h"
#include "Parser/Parser_Combatant_Info.h"
#include "Parser/Parser_Support.h"
#include "Core/StringInterner.h"
#include "Core/Utils/ColorUtils.h"  // awow::getActorTypeFromGuid
#include "Database/EmoteText.h"     // awow::emote::emoteTextFromTokens

#include <fstream>
#include <iostream>
#include <algorithm>
#include <charconv>

LiveLogSession::LiveLogSession() = default;

LiveLogSession::~LiveLogSession() {
    detach();
}

bool LiveLogSession::attach(const std::filesystem::path& logPath, bool scanExisting) {
    if (!std::filesystem::exists(logPath)) {
        return false;
    }

    detach();  // Clean up any previous session

    logPath_ = logPath;
    lastParsedOffset_ = 0;
    lastParseTime_ = std::chrono::steady_clock::now();
    pendingPartialLine_.clear();

    // Reset state
    liveBundle_.clear();
    historicalBundle_.clear();
    displayHistorical_ = false;
    viewState_ = SessionViewState::Live;
    guidToName_.clear();
    spellIdToName_.clear();
    guidToSpecId_.clear();
    encounters_.clear();
    firstCasts_.clear();
    emoteEvents_.clear();
    pullHistory_.clear();
    currentPull_ = PullSegment{};
    selectedPull_ = nullptr;
    nextPullNumber_ = 1;
    currentTrashNumber_ = 0;
    currentBossNumber_ = 0;
    combatState_ = CombatState::Idle;
    lastCombatEventTime_ms_ = 0;
    logStartTimestampSet_ = false;
    inMythicPlus_ = false;
    currentKeystoneLevel_ = 0;

    // Scan for segment boundaries (fast) instead of full parsing
    // (slow). Even the "fast" scan reads the whole file, which on a
    // multi-gig raid log takes several seconds - long enough that
    // the UI needs a spinner or the user thinks the app froze. Flip
    // parsingInProgress_ around it so the same meter-panel spinner
    // path lights up.
    if (scanExisting) {
        parsingInProgress_.store(true);
        scanForSegments();
        parsingInProgress_.store(false);
    }

    return true;
}

void LiveLogSession::detach() {
    logPath_.clear();
    lastParsedOffset_ = 0;
    pendingPartialLine_.clear();
}

size_t LiveLogSession::getCurrentFileSize() const {
    if (logPath_.empty()) return 0;

    try {
        return std::filesystem::file_size(logPath_);
    } catch (const std::filesystem::filesystem_error&) {
        return 0;
    }
}

bool LiveLogSession::poll() {
    if (!isAttached()) return false;

    // The live parse ALWAYS runs, even while the user is viewing a past
    // segment. Historical viewing reads a separate bundle
    // (historicalBundle_), so the live bundle can keep accumulating in
    // the background - returning to Current then shows the up-to-date
    // segment and no pull is ever dropped.

    // Check current file size
    size_t currentSize = getCurrentFileSize();
    if (currentSize <= lastParsedOffset_) {
        return false;  // No new data
    }

    // Open file for reading (with sharing so WoW can continue writing)
    std::ifstream file(logPath_, std::ios::binary);
    if (!file) {
        return false;
    }

    // Seek to where we left off
    file.seekg(lastParsedOffset_);
    if (!file) {
        return false;
    }

    // Read new data
    size_t bytesToRead = currentSize - lastParsedOffset_;
    std::string newData(bytesToRead, '\0');
    file.read(newData.data(), bytesToRead);
    size_t bytesRead = static_cast<size_t>(file.gcount());

    if (bytesRead == 0) {
        return false;
    }

    // Prepend any partial line from last read
    std::string fullBuffer = pendingPartialLine_ + newData.substr(0, bytesRead);
    pendingPartialLine_.clear();

    // Find the last complete line (ends with \n)
    size_t lastNewline = fullBuffer.rfind('\n');
    if (lastNewline == std::string::npos) {
        // No complete lines yet - save for next poll
        pendingPartialLine_ = fullBuffer;
        return false;
    }

    // Save any partial line at the end for next poll
    if (lastNewline < fullBuffer.size() - 1) {
        pendingPartialLine_ = fullBuffer.substr(lastNewline + 1);
        fullBuffer = fullBuffer.substr(0, lastNewline + 1);
    }

    // Tokenize the complete lines
    tokenized_segment tokenizer;
    auto result = tokenizer.tokenize(fullBuffer.c_str(), fullBuffer.size());

    if (result.empty()) {
        // Update offset even if no tokens (to skip blank lines)
        lastParsedOffset_ += bytesRead - pendingPartialLine_.size();
        return false;
    }

    // Feed the batch through in slices, re-taking the ActorMap lock per
    // slice. A busy poll can carry thousands of lines (~100ms of parse
    // work); holding the lock across all of it starves the render thread
    // into a spinner. Each slice ends on a line boundary, so the UI sees
    // a consistent prefix of the log between slices.
    constexpr size_t kLinesPerSlice = 256;
    size_t offsetEstimate = lastParsedOffset_;
    for (size_t firstLine = 0; firstLine < result.size(); firstLine += kLinesPerSlice) {
        std::lock_guard<std::timed_mutex> lock(actorMapMutex_);
        offsetEstimate = processLines(result, offsetEstimate, firstLine, kLinesPerSlice);
    }

    {
        std::lock_guard<std::timed_mutex> lock(actorMapMutex_);

        // Update offset
        lastParsedOffset_ += bytesRead - pendingPartialLine_.size();
        lastParseTime_ = std::chrono::steady_clock::now();

        // Update thread-safe snapshot for UI access
        updateSnapshot();
    }

    // Invoke data update callback (outside lock to avoid potential deadlock)
    if (onDataUpdate_) {
        onDataUpdate_();
    }

    return true;
}

size_t LiveLogSession::processLines(const std::vector<std::vector<std::string_view>>& lines,
                                    size_t startByteOffset,
                                    size_t firstLine,
                                    size_t lineCount) {
    size_t currentByteOffset = startByteOffset;
    size_t endLine = (lineCount >= lines.size() - firstLine) ? lines.size()
                                                             : firstLine + lineCount;

    for (size_t lineIdx = firstLine; lineIdx < endLine; ++lineIdx) {
        const auto& tokens = lines[lineIdx];
        if (tokens.size() < 3) {
            // Estimate line size for offset tracking
            for (const auto& t : tokens) currentByteOffset += t.size() + 1;
            continue;
        }

        std::string_view eventType = tokens[2];

        // Parse timestamp for this line (epoch ms; date-aware so pulls
        // spanning midnight keep increasing timestamps)
        int64_t absolute_ms = parseTimestampMs(tokens[0], tokens[1]);

        // Set log start timestamp from first line
        if (!logStartTimestampSet_ && absolute_ms > 0) {
            logStartTimestamp_ = absolute_ms;
            logStartTimestampSet_ = true;
        }

        // Make timestamp relative to log start
        int32_t timestamp_ms = static_cast<int32_t>(absolute_ms - logStartTimestamp_);

        // Handle encounter boundaries
        if (eventType == "ENCOUNTER_START") {
            handleEncounterStart(tokens, currentByteOffset);
        } else if (eventType == "ENCOUNTER_END") {
            handleEncounterEnd(tokens, currentByteOffset);
        } else if (eventType == "CHALLENGE_MODE_START") {
            handleChallengeModeStart(tokens);
        } else if (eventType == "CHALLENGE_MODE_END") {
            handleChallengeModeEnd(tokens);
        }

        // Check for combat idle timeout before processing new combat events
        if (combatState_ == CombatState::InCombat) {
            checkIdleTimeout(timestamp_ms, currentByteOffset);
        }

        // Track combat events for pull detection. All per-event parsing
        // lives in parseAndStoreEvent so that parseSegment / parseSegments
        // (historical replay) can reuse it without duplicating the
        // dispatch chain.
        bool isCombatEvent = parseAndStoreEvent(liveBundle_, tokens, eventType, timestamp_ms);
        // Update pull tracking if this was a combat event
        if (isCombatEvent) {
            handleCombatEvent(timestamp_ms, currentByteOffset);
        }

        // Estimate byte offset for next line
        for (const auto& t : tokens) currentByteOffset += t.size() + 1;
    }

    return currentByteOffset;
}

void LiveLogSession::handleCombatEvent(int32_t timestamp_ms, size_t byteOffset) {
    lastCombatEventTime_ms_ = timestamp_ms;

    if (combatState_ == CombatState::Idle) {
        // Start a new trash pull on first combat event
        ++currentTrashNumber_;
        std::string label = "Trash #" + std::to_string(currentTrashNumber_);
        startNewPull(timestamp_ms, byteOffset, label);
        currentPull_.segmentType = PullSegmentType::TrashPull;
        currentPull_.trashNumber = currentTrashNumber_;
        combatState_ = CombatState::InCombat;
    }
}

void LiveLogSession::handleEncounterStart(const std::vector<std::string_view>& tokens, size_t byteOffset) {
    if (tokens.size() < parser::EventParser<parser::EncounterStartTag>::expected_token_count()) {
        return;
    }

    auto data = parser::EventParser<parser::EncounterStartTag>::parse_and_return(tokens);
    int32_t timestamp_ms = static_cast<int32_t>(data.timestamp_ms - logStartTimestamp_);

    // End any preceding pull (trash or previous boss attempt) first.
    // endCurrentPull clears the live combat data so this new encounter
    // starts fresh.
    if (combatState_ != CombatState::Idle) {
        endCurrentPull(timestamp_ms, byteOffset);
        combatState_ = CombatState::Idle;
    }

    // First raid encounter of the session? Treat it like a "dungeon
    // start" for run tracking so raid clears surface in the UI the same
    // way M+ runs do. Do NOT clear pullHistory_ here anymore - the
    // segment dropdown should keep showing everything the user has
    // pulled this session, so they can go back to prior wipes.
    if (!inMythicPlus_ && encounters_.empty()) {
        ++currentDungeonRunId_;
        dungeonStartTime_ms_ = timestamp_ms;
        currentDungeonName_ = std::string(data.encounterName);
        if (onDungeonStart_) {
            onDungeonStart_(currentDungeonRunId_, currentDungeonName_, 0);
        }
    }

    // Start the encounter pull
    combatState_ = CombatState::InEncounter;
    ++currentBossNumber_;
    std::string label = inMythicPlus_
        ? "Boss " + std::to_string(currentBossNumber_) + ": " + std::string(data.encounterName)
        : std::string(data.encounterName);  // Raids use plain boss name
    // Keep the preceding trash data (clearData=false): the boss's own
    // records won't flush until the kill, and the live view falls back to
    // session-so-far in the meantime.
    startNewPull(timestamp_ms, byteOffset, label, /*clearData=*/false);

    // Mark this as an encounter
    currentPull_.isEncounter = true;
    currentPull_.segmentType = PullSegmentType::BossPull;
    currentPull_.bossNumber = currentBossNumber_;
    currentPull_.encounterId = data.encounterId;
    currentPull_.difficultyId = data.difficultyId;
    currentPull_.inMythicPlus = inMythicPlus_;
    currentPull_.keystoneLevel = currentKeystoneLevel_;

    // Add to encounters list
    EncounterSegment segment;
    segment.encounterName = std::string(data.encounterName);
    segment.encounterId = data.encounterId;
    segment.difficultyId = data.difficultyId;
    segment.startByteOffset = byteOffset;
    segment.startTimestamp_ms = static_cast<int32_t>(data.timestamp_ms - logStartTimestamp_);
    encounters_.push_back(segment);
}

void LiveLogSession::handleEncounterEnd(const std::vector<std::string_view>& tokens, size_t byteOffset) {
    if (tokens.size() < parser::EventParser<parser::EncounterEndTag>::expected_token_count()) {
        return;
    }

    auto data = parser::EventParser<parser::EncounterEndTag>::parse_and_return(tokens);
    int32_t timestamp_ms = static_cast<int32_t>(data.timestamp_ms - logStartTimestamp_);

    currentPull_.success = (data.success == 1);

    // Update the encounter segment
    if (!encounters_.empty()) {
        auto& segment = encounters_.back();
        segment.endByteOffset = byteOffset;
        segment.endTimestamp_ms = static_cast<int32_t>(data.timestamp_ms - logStartTimestamp_);
        segment.isSuccess = (data.success == 1);
    }

    endCurrentPull(timestamp_ms, byteOffset);
    combatState_ = CombatState::Idle;
}

void LiveLogSession::handleChallengeModeStart(const std::vector<std::string_view>& tokens) {
    if (tokens.size() < parser::EventParser<parser::ChallengeModeStartTag>::expected_token_count()) {
        return;
    }

    auto data = parser::EventParser<parser::ChallengeModeStartTag>::parse_and_return(tokens);
    int32_t timestamp_ms = static_cast<int32_t>(
        parseTimestampMs(tokens[0], tokens[1]) - logStartTimestamp_);

    // End any current pull before resetting. endCurrentPull already
    // clears the live combat data via clearLivePullData(), so we only
    // need to blow away the wider dungeon-scope state below.
    if (combatState_ != CombatState::Idle) {
        endCurrentPull(lastCombatEventTime_ms_, lastParsedOffset_);
        combatState_ = CombatState::Idle;
    } else {
        // No pull to close, but we still need a clean slate for the
        // new dungeon in case events accumulated since the last pull
        // ended (e.g. leftover buffs).
        clearLivePullData();
    }

    // Reset the per-run scope for the new dungeon, but do NOT clear
    // pullHistory_ - the selector groups by dungeonRunId, so prior runs
    // (earlier Algeth'ar attempts and the like) must stay in the list.
    // Keep guidToName_ and spellIdToName_ too - they're lookup caches
    // that help with name display across pulls.
    encounters_.clear();
    currentPull_ = PullSegment{};
    currentTrashNumber_ = 0;
    currentBossNumber_ = 0;
    lastCombatEventTime_ms_ = 0;

    // Track new dungeon run
    ++currentDungeonRunId_;
    dungeonStartTime_ms_ = timestamp_ms;
    currentDungeonName_ = std::string(data.zoneName);
    inMythicPlus_ = true;
    currentKeystoneLevel_ = data.keystoneLevel;

    // Remember where the run began so CHALLENGE_MODE_END can build the
    // "Overall - <dungeon>" segment spanning the whole key.
    challengeModeStartOffset_ = lastParsedOffset_;
    challengeModeStartTime_ms_ = timestamp_ms;

    // Notify UI of dungeon start
    if (onDungeonStart_) {
        onDungeonStart_(currentDungeonRunId_, currentDungeonName_, currentKeystoneLevel_);
    }
}

void LiveLogSession::handleChallengeModeEnd(const std::vector<std::string_view>& tokens) {
    int32_t timestamp_ms = (tokens.size() >= 2)
        ? static_cast<int32_t>(parseTimestampMs(tokens[0], tokens[1]) - logStartTimestamp_)
        : lastCombatEventTime_ms_;

    // End any current pull
    if (combatState_ != CombatState::Idle) {
        // Use last combat event time as end time
        endCurrentPull(lastCombatEventTime_ms_, lastParsedOffset_);
        combatState_ = CombatState::Idle;
    }

    // Add the "Overall - <dungeon>" segment spanning the whole key, the
    // same one the offline scan builds. Without this the completed run
    // has no aggregate entry in the live segment list. Its window runs
    // from the CHALLENGE_MODE_START to now, over the shared combat db.
    // Gate on inMythicPlus_ (the real "we're in a key" signal), not the
    // start byte offset - that's 0 for a key whose START landed in the
    // very first poll batch, since lastParsedOffset_ only advances after
    // the batch is processed.
    if (inMythicPlus_) {
        PullSegment overall;
        overall.label = "Overall - " + currentDungeonName_;
        overall.segmentType = PullSegmentType::DungeonOverall;
        overall.startByteOffset = challengeModeStartOffset_;
        overall.endByteOffset = lastParsedOffset_;
        overall.startTime_ms = challengeModeStartTime_ms_;
        overall.endTime_ms = timestamp_ms;
        overall.dungeonRunId = currentDungeonRunId_;
        overall.inMythicPlus = true;
        overall.keystoneLevel = currentKeystoneLevel_;
        overall.pullNumber = nextPullNumber_++;
        stampGroupInfo(overall);
        pullHistory_.push_back(overall);

        if (onPullEnd_) {
            onPullEnd_(overall);
        }
    }

    // Run finished: stamp its end time onto every segment of this run so
    // the group header reads "Algeth'ar Academy (12:37)".
    for (auto& seg : pullHistory_) {
        if (seg.dungeonRunId == currentDungeonRunId_ && seg.inMythicPlus) {
            seg.dungeonEndTime_ms = timestamp_ms;
        }
    }

    inMythicPlus_ = false;
    currentKeystoneLevel_ = 0;
    challengeModeStartOffset_ = 0;
}

void LiveLogSession::startNewPull(int32_t timestamp_ms, size_t byteOffset,
                                  const std::string& label, bool clearData) {
    // Deferred clear from the previous endCurrentPull(). We keep the
    // last pull's ActorMap around between pulls so the live view keeps
    // showing that pull's numbers instead of going blank while we wait
    // for the next combat event. The moment a new pull actually starts,
    // wipe and begin accumulating fresh.
    //
    // A boss ENCOUNTER_START passes clearData=false. Boss combat records
    // do not appear in the log until the kill flushes them, so if we
    // wiped here the live view would sit on an empty ActorMap for the
    // whole fight (ENCOUNTER_START..ENCOUNTER_END). Keeping the preceding
    // trash data around lets the live view fall back to session-so-far
    // until the boss records land; the boss segment's own [start,end]
    // window filters that older trash out the moment we snap to the boss
    // (getTimeRange's CurrentPull case). The next trash pull's
    // startNewPull() still clears, so memory stays bounded per inter-boss
    // block.
    if (clearData) {
        clearLivePullData();
    }

    currentPull_ = PullSegment{};
    currentPull_.startByteOffset = byteOffset;
    currentPull_.startTime_ms = timestamp_ms;
    currentPull_.label = label;
    currentPull_.pullNumber = nextPullNumber_++;
    currentPull_.inMythicPlus = inMythicPlus_;
    currentPull_.keystoneLevel = currentKeystoneLevel_;
    currentPull_.dungeonRunId = currentDungeonRunId_;
    stampGroupInfo(currentPull_);

    if (onPullStart_) {
        onPullStart_(currentPull_);
    }
}

void LiveLogSession::stampGroupInfo(PullSegment& seg) const {
    // Only M+ segments get a group header; standalone raid boss pulls
    // stay flat (empty dungeonName), so the selector renders them
    // without a collapsible wrapper. dungeonEndTime_ms stays 0 while the
    // run is live and is backfilled at CHALLENGE_MODE_END.
    if (!seg.inMythicPlus) return;
    seg.dungeonName = currentDungeonName_;
    seg.dungeonStartTime_ms = challengeModeStartTime_ms_;
}

void LiveLogSession::clearLivePullData() {
    liveBundle_.clear();
    firstCasts_.clear();
    emoteEvents_.clear();
}

void LiveLogSession::appendResourceSnapshot(CombatDataBundle& target,
                                            std::string_view source_guid,
                                            std::string_view dest_guid,
                                            const AdvancedUnitInfo& info,
                                            int32_t timestamp_ms) {
    // Combat log advanced info blocks describe whichever actor's
    // GUID matches info_guid. On damage events that's typically the
    // target; on heal events it can be source or target depending on
    // the spell. Route to the matching actor.
    std::string_view actorGuid;
    if (info.info_guid == source_guid) {
        actorGuid = source_guid;
    } else if (info.info_guid == dest_guid) {
        actorGuid = dest_guid;
    } else {
        return;  // synthetic or unknown - nothing useful to store
    }
    if (actorGuid.empty()) return;
    if (info.max_hp == 0) return;  // no advanced logging enabled

    ResourceStatusRecord record{};
    record.timestamp_ms = timestamp_ms;
    record.current_health = info.current_hp;
    record.max_health = info.max_hp;
    record.current_power = info.current_power;
    record.max_power = info.max_power;
    record.powertype = info.power_type;

    // resource_status_table must stay sorted by timestamp so
    // ResourceDatabase's binary-search lookup works. Live poll is
    // append-only in chronological order so a straight push_back
    // keeps the invariant. Segment replay builds into a fresh bundle
    // so the table always starts empty.
    target.actorMap[guidInterner().intern(actorGuid)].resource_status_table.push_back(record);
}

void LiveLogSession::endCurrentPull(int32_t timestamp_ms, size_t byteOffset) {
    currentPull_.endTime_ms = timestamp_ms;
    currentPull_.endByteOffset = byteOffset;

    // Add to history
    pullHistory_.push_back(currentPull_);

    // Do NOT clear ActorMap here. Keep the just-ended pull's data
    // visible in the live view until a new pull starts and calls
    // clearLivePullData() itself. This is what makes the overlay
    // continue showing the boss fight's stats after ENCOUNTER_END
    // instead of blanking until the first trash hit lands.

    if (onPullEnd_) {
        onPullEnd_(currentPull_);
    }
}

void LiveLogSession::checkIdleTimeout(int32_t /*currentTime_ms*/, size_t /*byteOffset*/) {
    // Idle gaps no longer split a trash segment. One trash segment now
    // spans an entire inter-boss gap: from the previous boss's end (or
    // the dungeon/session start) up to the next boss's ENCOUNTER_START,
    // accumulating across every combat burst and every quiet stretch in
    // between. Only a boss boundary (handleEncounterStart), a new M+
    // dungeon (handleChallengeModeStart) or a session reset closes a
    // trash segment now - not a lull in combat. Leaving the pull open
    // during idle is what collates the whole trash block into a single
    // entry in the segment list instead of "Trash #1..#12".
    //
    // Kept as a no-op hook (rather than deleting the call site) so the
    // combat-state machine and future idle-driven behavior have one
    // place to live.
}

void LiveLogSession::selectPull(size_t pullIndex) {
    if (pullIndex >= pullHistory_.size()) return;
    selectedPull_ = &pullHistory_[pullIndex];
}

void LiveLogSession::selectCurrentPull() {
    selectedPull_ = nullptr;
}

bool LiveLogSession::parseByteRangeInto(CombatDataBundle& target,
                                        size_t startByteOffset, size_t endByteOffset,
                                        int64_t segmentAbsoluteStart) {
    if (startByteOffset >= endByteOffset) return false;

    std::ifstream file(logPath_, std::ios::binary);
    if (!file) return false;

    file.seekg(static_cast<std::streamoff>(startByteOffset));
    if (!file) return false;

    size_t bytesToRead = endByteOffset - startByteOffset;
    std::string buffer(bytesToRead, '\0');
    file.read(buffer.data(), static_cast<std::streamsize>(bytesToRead));
    size_t bytesRead = static_cast<size_t>(file.gcount());
    if (bytesRead == 0) return false;

    std::string fullBuffer(buffer.data(), bytesRead);

    // Drop the partial line at the start (unless we're at file start) and
    // the partial line at the end - the byte window can slice mid-line.
    if (startByteOffset > 0) {
        size_t firstNewline = fullBuffer.find('\n');
        if (firstNewline != std::string::npos) {
            fullBuffer = fullBuffer.substr(firstNewline + 1);
        }
    }
    size_t lastNewline = fullBuffer.rfind('\n');
    if (lastNewline != std::string::npos && lastNewline < fullBuffer.size() - 1) {
        fullBuffer = fullBuffer.substr(0, lastNewline + 1);
    }
    if (fullBuffer.empty()) return false;

    tokenized_segment tokenizer;
    auto result = tokenizer.tokenize(fullBuffer.c_str(), fullBuffer.size());
    if (result.empty()) return false;

    // Historical replay is display-only: no combat-state or pull tracking,
    // just extract the events into the target bundle.
    for (const auto& tokens : result) {
        if (tokens.size() < 3) continue;
        std::string_view eventType = tokens[2];
        int32_t timestamp_ms = static_cast<int32_t>(
            parseTimestampMs(tokens[0], tokens[1]) - segmentAbsoluteStart);
        (void)parseAndStoreEvent(target, tokens, eventType, timestamp_ms);
    }

    return true;
}

bool LiveLogSession::parseSegment(const PullSegment& segment) {
    if (!isAttached()) return false;
    if (segment.startByteOffset >= segment.endByteOffset) return false;

    // Historical parse. Only parsingInProgress_ is raised for it (drives
    // the spinner); the live poll must keep running untouched, so we do
    // NOT take actorMapMutex_ for the parse itself. Build into a scratch
    // bundle under historicalMutex_, then publish it and flip the display
    // under actorMapMutex_ so the render thread reads a consistent pair.
    parsingInProgress_.store(true);

    // Rebase timestamps to the segment start so the drill-down chart,
    // headline duration and every derived per-second metric reads
    // relative to when THIS pull began instead of when the whole log
    // started. Matches the offline OutputWriter, which anchors on the
    // segment's ENCOUNTER_START. logStartTimestamp_ is set once during
    // the initial scan and never mutated afterward, so reading it here
    // off the worker thread is safe.
    const int64_t segmentAbsoluteStart = logStartTimestamp_ + segment.startTime_ms;

    CombatDataBundle scratch;
    {
        std::lock_guard<std::mutex> histLock(historicalMutex_);
        parseByteRangeInto(scratch, segment.startByteOffset,
                           segment.endByteOffset, segmentAbsoluteStart);
    }

    {
        // Publish: move the finished bundle in and point the display at
        // it. From here the historical bundle is immutable until the next
        // select or returnToLiveMode, so the render thread can read it
        // under this same lock without racing the live poll.
        std::lock_guard<std::timed_mutex> lock(actorMapMutex_);
        historicalBundle_ = std::move(scratch);
        displayHistorical_ = true;
        viewState_ = SessionViewState::Historical;
        updateSnapshot();
    }

    parsingInProgress_.store(false);

    if (onDataUpdate_) {
        onDataUpdate_();
    }

    return true;
}

bool LiveLogSession::parseSegments(const std::vector<PullSegment>& segments) {
    if (!isAttached()) return false;
    if (segments.empty()) return false;

    parsingInProgress_.store(true);

    // The Overall aggregation keeps log-relative timestamps (no per-pull
    // rebase), so subtract logStartTimestamp_ for every segment.
    CombatDataBundle scratch;
    {
        std::lock_guard<std::mutex> histLock(historicalMutex_);
        for (const auto& segment : segments) {
            parseByteRangeInto(scratch, segment.startByteOffset,
                               segment.endByteOffset, logStartTimestamp_);
        }
    }

    {
        std::lock_guard<std::timed_mutex> lock(actorMapMutex_);
        historicalBundle_ = std::move(scratch);
        displayHistorical_ = true;
        viewState_ = SessionViewState::Overall;
        updateSnapshot();
    }

    parsingInProgress_.store(false);

    if (onDataUpdate_) {
        onDataUpdate_();
    }

    return true;
}

void LiveLogSession::returnToLiveMode() {
    parsingInProgress_.store(true);

    {
        std::lock_guard<std::timed_mutex> lock(actorMapMutex_);

        // Point the display back at the live bundle. The live poll never
        // stopped, so it's already current - no re-parse, and crucially
        // NO skip-to-EOF: lastParsedOffset_ stays where the poll left it
        // so nothing written while viewing history is dropped.
        displayHistorical_ = false;
        viewState_ = SessionViewState::Live;

        // Free the historical bundle's memory now that it's off-screen.
        // clear() + swap-with-empty releases the ActorMap's buckets and
        // the event vectors' capacity so two-map mode costs nothing while
        // viewing live.
        historicalBundle_ = CombatDataBundle{};

        updateSnapshot();
    }

    parsingInProgress_.store(false);

    if (onDataUpdate_) {
        onDataUpdate_();
    }
}

void LiveLogSession::resetAll() {
    liveBundle_.clear();
    historicalBundle_.clear();
    displayHistorical_ = false;
    viewState_ = SessionViewState::Live;
    guidToName_.clear();
    spellIdToName_.clear();
    guidToSpecId_.clear();
    encounters_.clear();
    firstCasts_.clear();
    emoteEvents_.clear();
    pullHistory_.clear();
    petToOwnerFromSummons_.clear();
    currentPull_ = PullSegment{};
    selectedPull_ = nullptr;
    nextPullNumber_ = 1;
    combatState_ = CombatState::Idle;
    lastCombatEventTime_ms_ = 0;
}

void LiveLogSession::resetCurrentPull() {
    // Don't reset pull history, just the current accumulation
    // This is used when switching between pulls
}

void LiveLogSession::scanForSegments() {
    // Lightweight scan for segment boundaries only
    // Does NOT parse combat events into ActorMap - just finds byte offsets
    //
    // Trash segments are only tracked in M+ (Challenge Mode):
    // - Trash #1: CHALLENGE_MODE_START → first ENCOUNTER_START
    // - Trash #N: ENCOUNTER_END[N-1] → ENCOUNTER_START[N]
    // For raids (outside M+), we only track boss encounters.

    size_t currentSize = getCurrentFileSize();
    if (currentSize == 0) return;

    std::ifstream file(logPath_, std::ios::binary);
    if (!file) return;

    // Light up the meter-panel spinner while we work - this scan takes
    // seconds on a big raid log and used to run with no signal at all.
    // The dedicated scan flag lets the segment dropdown say what's
    // actually happening while results stream in.
    parsingInProgress_.store(true);
    segmentScanInProgress_.store(true);

    // Stream the file in fixed chunks instead of slurping it whole.
    // Raid logs run into gigabytes; a full-file std::string means a
    // multi-GB zero-filled allocation plus the entire read completing
    // before the first line is even looked at. Chunking keeps memory
    // flat and lets segments reach the dropdown while the scan runs.
    constexpr size_t kScanChunk = 8 * 1024 * 1024;
    std::string buffer;
    size_t chunkBase = 0;   // Absolute file offset of buffer[0]
    size_t lineStart = 0;   // Local index into buffer

    // Pulls the next chunk in, dropping everything before lineStart
    // (an unfinished line at the buffer end survives as the prefix).
    // Returns false once the file is exhausted.
    auto refill = [&]() -> bool {
        // lineStart can sit one past the end after the EOF partial line
        // (lineEnd == buffer.size(), then +1); erase throws on pos > size
        size_t consumed = (std::min)(lineStart, buffer.size());
        buffer.erase(0, consumed);
        chunkBase += consumed;
        lineStart = 0;

        size_t alreadyBuffered = buffer.size();
        size_t fileConsumed = chunkBase + alreadyBuffered;
        if (fileConsumed >= currentSize) {
            return false;
        }
        size_t toRead = (std::min)(kScanChunk, currentSize - fileConsumed);
        buffer.resize(alreadyBuffered + toRead);
        file.read(buffer.data() + alreadyBuffered, static_cast<std::streamsize>(toRead));
        size_t got = static_cast<size_t>(file.gcount());
        buffer.resize(alreadyBuffered + got);
        return got > 0;
    };

    // Boundary tracking for trash segments (M+ only). Absolute offsets.
    size_t lastBoundaryOffset = 0;      // End of last segment (for trash start)
    int32_t lastBoundaryTime_ms = 0;    // Time of last segment end

    // Publish newly found segments after each chunk so the segment
    // dropdown fills in while the scan is still running
    size_t publishedPulls = 0;
    auto publishProgress = [&]() {
        if (pullHistory_.size() != publishedPulls) {
            publishedPulls = pullHistory_.size();
            updateSnapshot();
            if (onDataUpdate_) {
                onDataUpdate_();
            }
        }
    };

    refill();

    // Scan line by line looking for boundary events only
    while (true) {
        if (lineStart >= buffer.size()) {
            publishProgress();
            if (!refill() || buffer.empty()) {
                break;
            }
        }

        // Find end of line
        size_t lineEnd = buffer.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            // Line may continue into the next chunk
            publishProgress();
            if (refill()) {
                continue;
            }
            if (lineStart >= buffer.size()) {
                break;
            }
            lineEnd = buffer.size();  // Partial final line at EOF
        }

        std::string_view line(buffer.data() + lineStart, lineEnd - lineStart);

        // Absolute file offsets for segment bookkeeping - lineStart and
        // lineEnd are local to the current buffer window
        const size_t absLineStart = chunkBase + lineStart;
        const size_t absLineEnd = chunkBase + lineEnd;

        // Fast-path reject: 99.99% of lines are SPELL_* / SWING_* /
        // RANGE_* damage or heal events. The boundary events we care
        // about all start with 'E' (ENCOUNTER_*) or 'C' (CHALLENGE_*).
        // WoW combat-log lines look like:
        //   "M/D/YYYY HH:MM:SS.mmm-Z  EVENT_NAME,..."
        // so the event name starts right after the run of two spaces
        // separating the timestamp field from the body. Look for that
        // gap and peek at the very first char of the event name; if it
        // is not one we might care about, skip the whole line without
        // doing any substring searches at all. That takes the per-line
        // work from "up to four std::string_view::find calls" down to
        // "one memchr + one char compare" on the reject path, which
        // is where the overwhelming majority of the ~10 million lines
        // in a raid log end up.
        bool isBoundaryEvent = false;
        std::string_view eventType;

        // Find the first comma (end of the event-type token). WoW
        // combat-log format: after the "  " separator the event name
        // is the first comma-delimited token. Locating the comma and
        // walking backwards to the space is cheaper than substring
        // matching all four candidate names.
        size_t commaPos = line.find(',');
        if (commaPos != std::string_view::npos && commaPos > 0) {
            // The event name spans [nameStart, commaPos). nameStart
            // sits just after the "  " gap; the header is fixed width
            // (~22-25 chars), so backing up from the comma to the
            // preceding space is O(event-name length).
            size_t nameStart = line.rfind(' ', commaPos - 1);
            if (nameStart != std::string_view::npos) {
                nameStart += 1;  // skip the space itself
                if (nameStart < commaPos) {
                    const char first = line[nameStart];
                    if (first == 'E' || first == 'C') {
                        std::string_view name =
                            line.substr(nameStart, commaPos - nameStart);
                        if (name == "ENCOUNTER_START") {
                            isBoundaryEvent = true;
                            eventType = "ENCOUNTER_START";
                        } else if (name == "ENCOUNTER_END") {
                            isBoundaryEvent = true;
                            eventType = "ENCOUNTER_END";
                        } else if (name == "CHALLENGE_MODE_START") {
                            isBoundaryEvent = true;
                            eventType = "CHALLENGE_MODE_START";
                        } else if (name == "CHALLENGE_MODE_END") {
                            isBoundaryEvent = true;
                            eventType = "CHALLENGE_MODE_END";
                        }
                    }
                }
            }
        }

        if (isBoundaryEvent) {
            // Tokenize just this line for parsing
            tokenized_segment tokenizer;
            std::string lineStr(line);
            auto result = tokenizer.tokenize(lineStr.c_str(), lineStr.size());

            if (!result.empty() && result[0].size() >= 3) {
                const auto& tokens = result[0];

                // Parse timestamp - tokens[0] is date, tokens[1] is time
                int64_t absolute_ms = parseTimestampMs(tokens[0], tokens[1]);
                if (!logStartTimestampSet_ && absolute_ms > 0) {
                    logStartTimestamp_ = absolute_ms;
                    logStartTimestampSet_ = true;
                }
                int32_t timestamp_ms = static_cast<int32_t>(absolute_ms - logStartTimestamp_);

                if (eventType == "CHALLENGE_MODE_START") {
                    // Start a fresh run WITHOUT wiping earlier runs - the
                    // selector groups by dungeonRunId, so prior Algeth'ar
                    // (or any) attempts stay in the list. Only reset the
                    // per-run counters and the in-flight pull; the new
                    // dungeonRunId below keeps this run's segments distinct.
                    encounters_.clear();
                    currentPull_ = PullSegment{};
                    currentTrashNumber_ = 0;
                    currentBossNumber_ = 0;
                    combatState_ = CombatState::Idle;

                    // Parse dungeon info
                    if (tokens.size() >= parser::EventParser<parser::ChallengeModeStartTag>::expected_token_count()) {
                        auto data = parser::EventParser<parser::ChallengeModeStartTag>::parse_and_return(tokens);
                        ++currentDungeonRunId_;
                        dungeonStartTime_ms_ = timestamp_ms;
                        currentDungeonName_ = std::string(data.zoneName);
                        inMythicPlus_ = true;
                        currentKeystoneLevel_ = data.keystoneLevel;

                        // Track challenge mode start for Overall segment
                        challengeModeStartOffset_ = absLineStart;
                        challengeModeStartTime_ms_ = timestamp_ms;

                        // Set first boundary for trash #1
                        lastBoundaryOffset = absLineEnd + 1;  // Start after this line
                        lastBoundaryTime_ms = timestamp_ms;
                    }
                } else if (eventType == "CHALLENGE_MODE_END") {
                    // Create final trash segment if there's content after last boss
                    if (inMythicPlus_ && lastBoundaryOffset > 0 && lastBoundaryOffset < absLineStart) {
                        ++currentTrashNumber_;
                        PullSegment trash;
                        trash.label = "Trash #" + std::to_string(currentTrashNumber_);
                        trash.segmentType = PullSegmentType::TrashPull;
                        trash.trashNumber = currentTrashNumber_;
                        trash.startByteOffset = lastBoundaryOffset;
                        trash.endByteOffset = absLineStart;
                        trash.startTime_ms = lastBoundaryTime_ms;
                        trash.endTime_ms = timestamp_ms;
                        trash.dungeonRunId = currentDungeonRunId_;
                        trash.inMythicPlus = true;
                        trash.keystoneLevel = currentKeystoneLevel_;
                        trash.pullNumber = nextPullNumber_++;
                        stampGroupInfo(trash);
                        pullHistory_.push_back(trash);
                    }

                    // Create Overall segment for the entire dungeon
                    if (inMythicPlus_ && challengeModeStartOffset_ > 0) {
                        PullSegment overall;
                        overall.label = "Overall - " + currentDungeonName_;
                        overall.segmentType = PullSegmentType::DungeonOverall;
                        overall.startByteOffset = challengeModeStartOffset_;
                        overall.endByteOffset = absLineEnd + 1;  // Include the CHALLENGE_MODE_END line
                        overall.startTime_ms = challengeModeStartTime_ms_;
                        overall.endTime_ms = timestamp_ms;
                        overall.dungeonRunId = currentDungeonRunId_;
                        overall.inMythicPlus = true;
                        overall.keystoneLevel = currentKeystoneLevel_;
                        overall.pullNumber = nextPullNumber_++;
                        stampGroupInfo(overall);
                        pullHistory_.push_back(overall);
                    }

                    // The run is over: backfill its end time onto every
                    // segment of this run so the group header can show a
                    // duration ("Algeth'ar Academy (12:37)").
                    for (auto& seg : pullHistory_) {
                        if (seg.dungeonRunId == currentDungeonRunId_ && seg.inMythicPlus) {
                            seg.dungeonEndTime_ms = timestamp_ms;
                        }
                    }

                    inMythicPlus_ = false;
                    lastBoundaryOffset = 0;
                    challengeModeStartOffset_ = 0;
                } else if (eventType == "ENCOUNTER_START") {
                    if (tokens.size() >= parser::EventParser<parser::EncounterStartTag>::expected_token_count()) {
                        auto data = parser::EventParser<parser::EncounterStartTag>::parse_and_return(tokens);

                        // If in M+, create trash segment from last boundary to this encounter
                        if (inMythicPlus_ && lastBoundaryOffset > 0 && lastBoundaryOffset < absLineStart) {
                            ++currentTrashNumber_;
                            PullSegment trash;
                            trash.label = "Trash #" + std::to_string(currentTrashNumber_);
                            trash.segmentType = PullSegmentType::TrashPull;
                            trash.trashNumber = currentTrashNumber_;
                            trash.startByteOffset = lastBoundaryOffset;
                            trash.endByteOffset = absLineStart;
                            trash.startTime_ms = lastBoundaryTime_ms;
                            trash.endTime_ms = timestamp_ms;
                            trash.dungeonRunId = currentDungeonRunId_;
                            trash.inMythicPlus = true;
                            trash.keystoneLevel = currentKeystoneLevel_;
                            trash.pullNumber = nextPullNumber_++;
                            stampGroupInfo(trash);
                            pullHistory_.push_back(trash);
                        }

                        // If not in M+, this starts a new run (raid)
                        if (!inMythicPlus_ && encounters_.empty()) {
                            ++currentDungeonRunId_;
                            dungeonStartTime_ms_ = timestamp_ms;
                            currentDungeonName_ = std::string(data.encounterName);
                        }

                        // Start boss pull
                        combatState_ = CombatState::InEncounter;
                        ++currentBossNumber_;
                        std::string label = inMythicPlus_
                            ? "Boss " + std::to_string(currentBossNumber_) + ": " + std::string(data.encounterName)
                            : std::string(data.encounterName);
                        startNewPull(timestamp_ms, absLineStart, label);

                        currentPull_.isEncounter = true;
                        currentPull_.segmentType = PullSegmentType::BossPull;
                        currentPull_.bossNumber = currentBossNumber_;
                        currentPull_.encounterId = data.encounterId;
                        currentPull_.difficultyId = data.difficultyId;
                        currentPull_.inMythicPlus = inMythicPlus_;
                        currentPull_.keystoneLevel = currentKeystoneLevel_;

                        EncounterSegment segment;
                        segment.encounterName = std::string(data.encounterName);
                        segment.encounterId = data.encounterId;
                        segment.difficultyId = data.difficultyId;
                        segment.startByteOffset = absLineStart;
                        segment.startTimestamp_ms = timestamp_ms;
                        encounters_.push_back(segment);
                    }
                } else if (eventType == "ENCOUNTER_END") {
                    if (tokens.size() >= parser::EventParser<parser::EncounterEndTag>::expected_token_count()) {
                        auto data = parser::EventParser<parser::EncounterEndTag>::parse_and_return(tokens);
                        currentPull_.success = (data.success == 1);

                        if (!encounters_.empty()) {
                            auto& segment = encounters_.back();
                            segment.endByteOffset = absLineEnd;
                            segment.endTimestamp_ms = timestamp_ms;
                            segment.isSuccess = (data.success == 1);
                        }

                        endCurrentPull(timestamp_ms, absLineEnd);
                        combatState_ = CombatState::Idle;

                        // Update boundary for next trash segment (M+ only)
                        if (inMythicPlus_) {
                            lastBoundaryOffset = absLineEnd + 1;  // Start after this line
                            lastBoundaryTime_ms = timestamp_ms;
                        }
                    }
                }

                lastCombatEventTime_ms_ = timestamp_ms;
            }
        }

        lineStart = lineEnd + 1;
    }

    // Set offset to everything we consumed so poll() continues from here
    lastParsedOffset_ = chunkBase + buffer.size();

    segmentScanInProgress_.store(false);
    parsingInProgress_.store(false);

    // Update snapshot for UI
    updateSnapshot();

    // Notify UI that segments are available
    if (onDataUpdate_) {
        onDataUpdate_();
    }
}

int64_t LiveLogSession::parseTimestampMs(std::string_view date, std::string_view time) {
    // Delegate to the parser's date-aware conversion (it caches the
    // date-to-epoch step, so this costs a handful of from_chars calls
    // per line). This also puts pull times on the same epoch base as
    // the parser-produced encounter timestamps, which used to sit on
    // two different clocks.
    thread_local std::vector<std::string_view> tokens(2);
    tokens[0] = date;
    tokens[1] = time;
    return static_cast<int64_t>(parser::EventParser<void>::parse_timestamp_ms(tokens));
}

LiveLogSession::Snapshot LiveLogSession::getSnapshot() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_;
}

std::unique_lock<std::timed_mutex> LiveLogSession::tryLockActorMap() const {
    // Wait briefly rather than a bare try_lock: incremental polls hold
    // the lock a few milliseconds per slice, so a short wait rides out a
    // slice instead of costing the caller a spinner frame. Long holds
    // (historical segment re-parse) still time out and fall back.
    return std::unique_lock<std::timed_mutex>(actorMapMutex_, std::chrono::milliseconds(10));
}

std::unique_lock<std::timed_mutex> LiveLogSession::lockActorMap() const {
    return std::unique_lock<std::timed_mutex>(actorMapMutex_);
}

void LiveLogSession::updateSnapshot() {
    std::lock_guard<std::mutex> lock(snapshotMutex_);

    // Name lookups
    snapshot_.guidToName = guidToName_;
    snapshot_.spellIdToName = spellIdToName_;

    // Current pull state
    snapshot_.currentPull = currentPull_;
    snapshot_.inCombat = (combatState_ != CombatState::Idle);
    snapshot_.inEncounter = (combatState_ == CombatState::InEncounter);
    snapshot_.inMythicPlus = inMythicPlus_;

    // Pull history metadata (lightweight - each PullSegment is ~80 bytes)
    // This allows UI to show pull dropdown without expensive ActorMap copies
    snapshot_.pullHistory = pullHistory_;

    // Phase-rule inputs and spec ids (all small; see the member docs)
    snapshot_.firstCasts = firstCasts_;
    snapshot_.emotes = emoteEvents_;
    snapshot_.guidToSpecId = guidToSpecId_;
}

uint32_t LiveLogSession::resolveOwnerGuidId(std::string_view source_guid,
                                            std::string_view advanced_owner_guid) {
    // First choice: advanced combat log block. Only trust it when the
    // owner is a Player - the "0000000000000000" placeholder and any
    // NPC-owned guardian gets rejected here.
    if (!advanced_owner_guid.empty() &&
        advanced_owner_guid != "0000000000000000" &&
        advanced_owner_guid.starts_with("Player-")) {
        return guidInterner().intern(advanced_owner_guid);
    }

    // Fallback: pet lineage learned from SPELL_SUMMON. This is what
    // rescues Wild Imps and other short-lived pets whose damage
    // events ship without owner info in the advanced block.
    auto it = petToOwnerFromSummons_.find(std::string(source_guid));
    if (it != petToOwnerFromSummons_.end()) {
        return guidInterner().intern(it->second);
    }

    return 0;
}

std::unordered_map<StringInterner::Id, StringInterner::Id>
LiveLogSession::getSummonPetToOwnerMap() const {
    std::unordered_map<StringInterner::Id, StringInterner::Id> out;
    out.reserve(petToOwnerFromSummons_.size());
    for (const auto& [pet_guid, owner_guid] : petToOwnerFromSummons_) {
        out[guidInterner().intern(pet_guid)] = guidInterner().intern(owner_guid);
    }
    return out;
}

bool LiveLogSession::parseAndStoreEvent(CombatDataBundle& target,
                                        const std::vector<std::string_view>& tokens,
                                        std::string_view eventType,
                                        int32_t timestamp_ms) {
    bool isCombatEvent = false;

    // DAMAGE EVENTS
    if (eventType == "SPELL_DAMAGE" || eventType == "RANGE_DAMAGE" ||
        eventType == "SPELL_PERIODIC_DAMAGE" || eventType == "SWING_DAMAGE_LANDED" ||
        eventType == "DAMAGE_SPLIT") {
        isCombatEvent = true;

        if (eventType == "SPELL_DAMAGE" || eventType == "RANGE_DAMAGE") {
            if (tokens.size() >= parser::EventParser<parser::SpellDamageTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellDamageTag>::parse_and_return(tokens);
                if (!data.source_guid.empty() && !data.source_name.empty()) {
                    guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
                }
                if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                    guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
                }
                if (data.spell.spell_id > 0 && !data.spell.spell_name.empty()) {
                    spellIdToName_.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));
                }
                CombatRecord record{};
                record.timestamp_ms = timestamp_ms;
                record.spell_id = data.spell.spell_id;
                record.amount = data.amount;
                record.effective_amount = data.mitigated_amount;
                record.absorbed = data.absorbed;
                record.blocked = data.blocked;
                record.resisted = data.resisted;
                record.target_guid_id = guidInterner().intern(data.dest_guid);
                record.owner_guid_id = resolveOwnerGuidId(data.source_guid,
                                                          data.advanced_info.owner_guid);
                record.spell_school = static_cast<uint8_t>(data.damage_school);
                record.event_type = CombatEventType::DamageDealt;
                if (data.critical) record.flags |= static_cast<uint16_t>(CombatEventFlags::Critical);
                // Friendly fire counts as damage taken, not damage done
                if (data.source_flags.isFriendly() && data.dest_flags.isFriendly()) {
                    record.flags |= static_cast<uint16_t>(CombatEventFlags::FriendlyFire);
                }
                target.actorMap[guidInterner().intern(data.source_guid)].damage_dealt_table.push_back(record);
                appendResourceSnapshot(target, data.source_guid, data.dest_guid,
                                       data.advanced_info, timestamp_ms);
            }
        } else if (eventType == "SPELL_PERIODIC_DAMAGE") {
            if (tokens.size() >= parser::EventParser<parser::SpellPeriodicDamageTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellPeriodicDamageTag>::parse_and_return(tokens);
                if (!data.source_guid.empty() && !data.source_name.empty()) {
                    guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
                }
                if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                    guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
                }
                if (data.spell.spell_id > 0 && !data.spell.spell_name.empty()) {
                    spellIdToName_.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));
                }
                CombatRecord record{};
                record.timestamp_ms = timestamp_ms;
                record.spell_id = data.spell.spell_id;
                record.amount = data.amount;
                record.effective_amount = data.mitigated_amount;
                record.absorbed = data.absorbed;
                record.blocked = data.blocked;
                record.resisted = data.resisted;
                record.target_guid_id = guidInterner().intern(data.dest_guid);
                record.owner_guid_id = resolveOwnerGuidId(data.source_guid,
                                                          data.advanced_info.owner_guid);
                record.spell_school = static_cast<uint8_t>(data.damage_school);
                record.event_type = CombatEventType::DamageDealt;
                record.flags |= static_cast<uint16_t>(CombatEventFlags::Periodic);
                if (data.critical) record.flags |= static_cast<uint16_t>(CombatEventFlags::Critical);
                // Friendly fire counts as damage taken, not damage done
                if (data.source_flags.isFriendly() && data.dest_flags.isFriendly()) {
                    record.flags |= static_cast<uint16_t>(CombatEventFlags::FriendlyFire);
                }
                target.actorMap[guidInterner().intern(data.source_guid)].damage_dealt_table.push_back(record);
                appendResourceSnapshot(target, data.source_guid, data.dest_guid,
                                       data.advanced_info, timestamp_ms);
            }
        } else if (eventType == "SWING_DAMAGE_LANDED") {
            if (tokens.size() >= parser::EventParser<parser::SwingDamageLandedTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SwingDamageLandedTag>::parse_and_return(tokens);
                if (!data.source_guid.empty() && !data.source_name.empty()) {
                    guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
                }
                if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                    guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
                }
                CombatRecord record{};
                record.timestamp_ms = timestamp_ms;
                record.spell_id = 0;
                record.amount = data.amount;
                record.effective_amount = data.mitigated_amount;
                record.absorbed = data.absorbed;
                record.blocked = data.blocked;
                record.resisted = data.resisted;
                record.target_guid_id = guidInterner().intern(data.dest_guid);
                record.owner_guid_id = resolveOwnerGuidId(data.source_guid,
                                                          data.advanced_info.owner_guid);
                record.spell_school = static_cast<uint8_t>(data.spell.spell_school);
                record.event_type = CombatEventType::DamageDealt;
                record.flags |= static_cast<uint16_t>(CombatEventFlags::IsSwing);
                if (data.critical) record.flags |= static_cast<uint16_t>(CombatEventFlags::Critical);
                // Friendly fire counts as damage taken, not damage done
                if (data.source_flags.isFriendly() && data.dest_flags.isFriendly()) {
                    record.flags |= static_cast<uint16_t>(CombatEventFlags::FriendlyFire);
                }
                target.actorMap[guidInterner().intern(data.source_guid)].damage_dealt_table.push_back(record);
                appendResourceSnapshot(target, data.source_guid, data.dest_guid,
                                       data.advanced_info, timestamp_ms);
            }
        }
    }
    // HEALING EVENTS
    else if (eventType == "SPELL_HEAL" || eventType == "SPELL_PERIODIC_HEAL") {
        isCombatEvent = true;

        if (eventType == "SPELL_HEAL") {
            if (tokens.size() >= parser::EventParser<parser::SpellHealTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellHealTag>::parse_and_return(tokens);
                if (!data.source_guid.empty() && !data.source_name.empty()) {
                    guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
                }
                if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                    guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
                }
                if (data.spell.spell_id > 0 && !data.spell.spell_name.empty()) {
                    spellIdToName_.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));
                }
                CombatRecord record{};
                record.timestamp_ms = timestamp_ms;
                record.spell_id = data.spell.spell_id;
                record.amount = data.amount;
                record.effective_amount = data.amount - data.overhealing;
                record.absorbed = data.absorbed;
                record.target_guid_id = guidInterner().intern(data.dest_guid);
                record.owner_guid_id = resolveOwnerGuidId(data.source_guid,
                                                          data.advanced_info.owner_guid);
                record.spell_school = static_cast<uint8_t>(data.spell.spell_school);
                record.event_type = CombatEventType::HealingDone;
                if (data.critical) record.flags |= static_cast<uint16_t>(CombatEventFlags::Critical);
                if (data.overhealing > 0) record.flags |= static_cast<uint16_t>(CombatEventFlags::Overheal);
                target.actorMap[guidInterner().intern(data.source_guid)].healing_done_table.push_back(record);
                appendResourceSnapshot(target, data.source_guid, data.dest_guid,
                                       data.advanced_info, timestamp_ms);
            }
        } else if (eventType == "SPELL_PERIODIC_HEAL") {
            if (tokens.size() >= parser::EventParser<parser::SpellPeriodicHealTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellPeriodicHealTag>::parse_and_return(tokens);
                if (!data.source_guid.empty() && !data.source_name.empty()) {
                    guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
                }
                if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                    guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
                }
                if (data.spell.spell_id > 0 && !data.spell.spell_name.empty()) {
                    spellIdToName_.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));
                }
                CombatRecord record{};
                record.timestamp_ms = timestamp_ms;
                record.spell_id = data.spell.spell_id;
                record.amount = data.amount;
                record.effective_amount = data.amount - data.overhealing;
                record.absorbed = data.absorbed;
                record.target_guid_id = guidInterner().intern(data.dest_guid);
                record.owner_guid_id = resolveOwnerGuidId(data.source_guid,
                                                          data.advanced_info.owner_guid);
                record.spell_school = static_cast<uint8_t>(data.spell.spell_school);
                record.event_type = CombatEventType::HealingDone;
                record.flags |= static_cast<uint16_t>(CombatEventFlags::Periodic);
                if (data.critical) record.flags |= static_cast<uint16_t>(CombatEventFlags::Critical);
                if (data.overhealing > 0) record.flags |= static_cast<uint16_t>(CombatEventFlags::Overheal);
                target.actorMap[guidInterner().intern(data.source_guid)].healing_done_table.push_back(record);
                appendResourceSnapshot(target, data.source_guid, data.dest_guid,
                                       data.advanced_info, timestamp_ms);
            }
        }
    }
    // SUPPORT EVENTS (Augmentation Evoker attribution)
    // The *_SUPPORT damage/heal lines are duplicates of the parent event
    // that the log emits so meters can credit the supporter (Aug Evoker
    // or similar) for the buff-derived portion of the damage/heal. The
    // combat record is attributed to supporter_guid, not source_guid,
    // and carries the Support flag so downstream views can distinguish
    // it from the actor's own damage.
    else if (eventType == "SPELL_DAMAGE_SUPPORT" ||
             eventType == "SPELL_PERIODIC_DAMAGE_SUPPORT" ||
             eventType == "SWING_DAMAGE_LANDED_SUPPORT") {
        isCombatEvent = true;

        auto storeSupport = [&](const parser::SupportEventData& data,
                                bool isPeriodic, bool isSwing) {
            if (!data.source_guid.empty() && !data.source_name.empty()) {
                guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
            }
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            if (data.spell.spell_id > 0 && !data.spell.spell_name.empty()) {
                spellIdToName_.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));
            }
            if (data.supporter_guid.empty()) return;

            // Match the LIVE parent-record storage convention: raw
            // data.amount into record.amount and raw data.mitigated
            // into record.effective_amount. The deduction below has
            // to subtract the same shape from the parent record, or
            // the crit-doubling / overkill-subtraction that offline
            // does would knock the parent's amount 2x too low on every
            // crit and pile the shadow onto Aug at inflated size.
            // That is exactly what caused the Ebon Might row to read
            // Effective 17.7M against Raw 10.6M in-game: Aug's shadow
            // was crit-doubled while the amplified players' parents
            // were not.
            const int64_t supportAmount = data.amount;
            const int64_t supportEffective = data.mitigated_amount;

            // Deduct the support slice from the amplified actor's
            // parent record. Match rules (validated on 20 real
            // samples from a live raid log):
            //   - same source_guid (the amplified actor)
            //   - same target_guid (dest of the support event)
            //   - same damage_school
            //   - SWING support <-> SWING parent; SPELL/RANGE support
            //     <-> SPELL/RANGE parent
            //   - timestamp within +/- 2ms slop
            //   - skip records already flagged Support (never match a
            //     shadow to another shadow)
            // First hit going backward wins. On no match we still
            // credit the supporter; the amplified actor keeps their
            // full parent value (tiny drift accepted).
            const uint32_t target_id = guidInterner().intern(data.dest_guid);
            const uint8_t school = static_cast<uint8_t>(
                isSwing ? data.spell.spell_school : data.damage_school);
            if (!data.source_guid.empty()) {
                auto srcIt = target.actorMap.find(guidInterner().find(data.source_guid));
                if (srcIt != target.actorMap.end()) {
                    auto& parent_table = srcIt->second.damage_dealt_table;
                    for (auto rit = parent_table.rbegin();
                         rit != parent_table.rend();
                         ++rit) {
                        if (timestamp_ms - rit->timestamp_ms > 2) break;
                        if (rit->flags & CombatEventFlags::Support) continue;
                        if (rit->target_guid_id != target_id) continue;
                        if (rit->spell_school != school) continue;
                        const bool parent_is_swing =
                            (rit->flags & CombatEventFlags::IsSwing) != 0;
                        if (parent_is_swing != isSwing) continue;
                        rit->amount -= supportAmount;
                        rit->effective_amount -= supportEffective;
                        break;
                    }
                }
            }

            CombatRecord record{};
            record.timestamp_ms = timestamp_ms;
            // SWING_DAMAGE_LANDED_SUPPORT ships with the supporter's
            // aura spell_id (e.g. Ebon Might 395152), NOT the melee
            // swing. Store the real spell_id in both cases so meter
            // rows for Aug's support contribution collapse under one
            // aura row instead of splitting SPELL support into an
            // Ebon Might row and SWING support into a phantom
            // "Spell 0" / melee row.
            record.spell_id = data.spell.spell_id;
            record.amount = supportAmount;
            record.effective_amount = supportEffective;
            record.absorbed = data.absorbed;
            record.blocked = data.blocked;
            record.resisted = data.resisted;
            record.target_guid_id = target_id;
            record.owner_guid_id = 0;  // Support attribution goes to supporter directly
            record.spell_school = school;
            record.event_type = CombatEventType::DamageDealt;
            record.flags |= static_cast<uint16_t>(CombatEventFlags::Support);
            if (isPeriodic) record.flags |= static_cast<uint16_t>(CombatEventFlags::Periodic);
            if (isSwing)    record.flags |= static_cast<uint16_t>(CombatEventFlags::IsSwing);
            if (data.critical) record.flags |= static_cast<uint16_t>(CombatEventFlags::Critical);

            target.actorMap[guidInterner().intern(data.supporter_guid)].damage_dealt_table.push_back(record);
        };

        if (eventType == "SPELL_DAMAGE_SUPPORT") {
            if (tokens.size() >= parser::EventParser<parser::SpellDamageSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellDamageSupportTag>::parse_and_return(tokens);
                storeSupport(data, false, false);
            }
        } else if (eventType == "SPELL_PERIODIC_DAMAGE_SUPPORT") {
            if (tokens.size() >= parser::EventParser<parser::SpellPeriodicDamageSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellPeriodicDamageSupportTag>::parse_and_return(tokens);
                // Offline OutputWriter deliberately does NOT set the
                // Support ticks keep the periodic flag of the tick they ride on.
                storeSupport(data, false, false);
            }
        } else {  // SWING_DAMAGE_LANDED_SUPPORT
            if (tokens.size() >= parser::EventParser<parser::SwingDamageLandedSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SwingDamageLandedSupportTag>::parse_and_return(tokens);
                storeSupport(data, false, true);
            }
        }
    }
    else if (eventType == "SPELL_HEAL_SUPPORT" ||
             eventType == "SPELL_PERIODIC_HEAL_SUPPORT") {
        isCombatEvent = true;

        auto storeHealSupport = [&](const parser::HealSupportEventData& data) {
            if (!data.source_guid.empty() && !data.source_name.empty()) {
                guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
            }
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            if (data.spell.spell_id > 0 && !data.spell.spell_name.empty()) {
                spellIdToName_.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));
            }
            if (data.supporter_guid.empty()) return;

            const int64_t supportAmount = data.amount;
            const int64_t supportEffective = data.amount - data.overhealing;
            const uint32_t target_id = guidInterner().intern(data.dest_guid);
            const uint8_t school = static_cast<uint8_t>(data.spell.spell_school);

            // Same deduction rules as damage support, applied to the
            // amplified healer's parent heal record. Skip already-flagged
            // Support records so shadows never match shadows.
            if (!data.source_guid.empty()) {
                auto srcIt = target.actorMap.find(guidInterner().find(data.source_guid));
                if (srcIt != target.actorMap.end()) {
                    auto& parent_table = srcIt->second.healing_done_table;
                    for (auto rit = parent_table.rbegin();
                         rit != parent_table.rend();
                         ++rit) {
                        if (timestamp_ms - rit->timestamp_ms > 2) break;
                        if (rit->flags & CombatEventFlags::Support) continue;
                        if (rit->target_guid_id != target_id) continue;
                        if (rit->spell_school != school) continue;
                        rit->amount -= supportAmount;
                        rit->effective_amount -= supportEffective;
                        break;
                    }
                }
            }

            CombatRecord record{};
            record.timestamp_ms = timestamp_ms;
            record.spell_id = data.spell.spell_id;
            record.amount = supportAmount;
            record.effective_amount = supportEffective;
            record.absorbed = data.absorbed;
            record.target_guid_id = target_id;
            record.owner_guid_id = 0;
            record.spell_school = school;
            record.event_type = CombatEventType::HealingDone;
            record.flags |= static_cast<uint16_t>(CombatEventFlags::Support);
            if (data.critical) record.flags |= static_cast<uint16_t>(CombatEventFlags::Critical);
            if (data.overhealing > 0) record.flags |= static_cast<uint16_t>(CombatEventFlags::Overheal);

            target.actorMap[guidInterner().intern(data.supporter_guid)].healing_done_table.push_back(record);
        };

        if (eventType == "SPELL_HEAL_SUPPORT") {
            if (tokens.size() >= parser::EventParser<parser::SpellHealSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellHealSupportTag>::parse_and_return(tokens);
                storeHealSupport(data);
            }
        } else {  // SPELL_PERIODIC_HEAL_SUPPORT
            if (tokens.size() >= parser::EventParser<parser::SpellPeriodicHealSupportTag>::expected_token_count()) {
                auto data = parser::EventParser<parser::SpellPeriodicHealSupportTag>::parse_and_return(tokens);
                storeHealSupport(data);
            }
        }
    }
    // ABSORB EVENTS
    else if (eventType == "SPELL_ABSORBED") {
        isCombatEvent = true;
        if (tokens.size() >= parser::EventParser<parser::SpellAbsorbedTag>::minimum_token_count()) {
            auto data = parser::EventParser<parser::SpellAbsorbedTag>::parse_and_return(tokens);
            if (!data.source_guid.empty() && !data.source_name.empty()) {
                guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
            }
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            if (!data.absorber_guid.empty() && !data.absorber_name.empty()) {
                guidToName_.try_emplace(std::string(data.absorber_guid), std::string(data.absorber_name));
            }
            if (data.absorb_spell_id > 0 && !data.absorb_spell_name.empty()) {
                spellIdToName_.try_emplace(data.absorb_spell_id, std::string(data.absorb_spell_name));
            }
            AbsorbEvent ev;
            ev.source_guid = std::string(data.source_guid);
            ev.source_name = std::string(data.source_name);
            ev.target_guid = std::string(data.dest_guid);
            ev.target_name = std::string(data.dest_name);
            ev.absorber_guid = std::string(data.absorber_guid);
            ev.absorber_name = std::string(data.absorber_name);
            ev.source_spell_id = data.spell.spell_id;
            ev.source_spell_name = std::string(data.spell.spell_name);
            ev.absorb_spell_id = data.absorb_spell_id;
            ev.absorb_spell_name = std::string(data.absorb_spell_name);
            ev.timestamp_ms = timestamp_ms;
            ev.absorbed_amount = data.amount;
            ev.total_damage = 0;
            target.absorbEvents.push_back(std::move(ev));
        }
    }
    else if (eventType == "SPELL_ABSORBED_SUPPORT") {
        isCombatEvent = true;
        if (tokens.size() >= parser::EventParser<parser::SpellAbsorbedSupportTag>::minimum_token_count()) {
            auto data = parser::EventParser<parser::SpellAbsorbedSupportTag>::parse_and_return(tokens);
            if (!data.source_guid.empty() && !data.source_name.empty()) {
                guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
            }
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            if (!data.absorber_guid.empty() && !data.absorber_name.empty()) {
                guidToName_.try_emplace(std::string(data.absorber_guid), std::string(data.absorber_name));
            }
            if (data.absorb_spell_id > 0 && !data.absorb_spell_name.empty()) {
                spellIdToName_.try_emplace(data.absorb_spell_id, std::string(data.absorb_spell_name));
            }
            AbsorbEvent ev;
            ev.source_guid = std::string(data.source_guid);
            ev.source_name = std::string(data.source_name);
            ev.target_guid = std::string(data.dest_guid);
            ev.target_name = std::string(data.dest_name);
            ev.absorber_guid = std::string(data.absorber_guid);
            ev.absorber_name = std::string(data.absorber_name);
            ev.source_spell_id = data.spell.spell_id;
            ev.source_spell_name = std::string(data.spell.spell_name);
            ev.absorb_spell_id = data.absorb_spell_id;
            ev.absorb_spell_name = std::string(data.absorb_spell_name);
            ev.timestamp_ms = timestamp_ms;
            ev.absorbed_amount = data.amount;
            ev.total_damage = 0;
            ev.supporter_guid = std::string(data.supporter_guid);
            target.absorbEvents.push_back(std::move(ev));
        }
    }
    // MISSED / AVOIDANCE
    else if (eventType == "SPELL_MISSED" || eventType == "SPELL_PERIODIC_MISSED"
          || eventType == "RANGE_MISSED") {
        isCombatEvent = true;
        if (tokens.size() >= parser::EventParser<parser::SpellMissedTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellMissedTag>::parse_and_return(tokens);
            if (!data.source_guid.empty() && !data.source_name.empty()) {
                guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
            }
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            if (data.spell.spell_id > 0 && !data.spell.spell_name.empty()) {
                spellIdToName_.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));
            }
            MissedEvent ev;
            ev.source_guid = std::string(data.source_guid);
            ev.source_name = std::string(data.source_name);
            ev.target_guid = std::string(data.dest_guid);
            ev.target_name = std::string(data.dest_name);
            ev.spell_id = data.spell.spell_id;
            ev.spell_name = std::string(data.spell.spell_name);
            ev.timestamp_ms = timestamp_ms;
            ev.miss_type = data.miss_type;
            ev.amount_missed = data.amount_missed;
            ev.blocked_amount = data.blocked_amount;
            ev.is_offhand = data.is_off_hand;
            ev.is_critical = data.critical;
            ev.is_periodic = (eventType == "SPELL_PERIODIC_MISSED");
            target.missedEvents.push_back(std::move(ev));
        }
    }
    else if (eventType == "SWING_MISSED") {
        isCombatEvent = true;
        if (tokens.size() >= parser::EventParser<parser::SwingMissedTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SwingMissedTag>::parse_and_return(tokens);
            if (!data.source_guid.empty() && !data.source_name.empty()) {
                guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
            }
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            MissedEvent ev;
            ev.source_guid = std::string(data.source_guid);
            ev.source_name = std::string(data.source_name);
            ev.target_guid = std::string(data.dest_guid);
            ev.target_name = std::string(data.dest_name);
            ev.spell_id = 1;
            ev.spell_name = "Melee";
            ev.timestamp_ms = timestamp_ms;
            ev.miss_type = data.miss_type;
            ev.amount_missed = data.amount_missed;
            ev.blocked_amount = 0;
            ev.is_offhand = data.is_off_hand;
            ev.is_critical = data.critical;
            ev.is_periodic = false;
            target.missedEvents.push_back(std::move(ev));
        }
    }
    // DISPEL / INTERRUPT / STOLEN
    else if (eventType == "SPELL_DISPEL") {
        isCombatEvent = true;
        if (tokens.size() >= parser::EventParser<parser::SpellDispelTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellDispelTag>::parse_and_return(tokens);
            if (!data.source_guid.empty() && !data.source_name.empty()) {
                guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
            }
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            if (data.spell.spell_id > 0 && !data.spell.spell_name.empty()) {
                spellIdToName_.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));
            }
            DispelInterruptEvent ev;
            ev.source_guid = std::string(data.source_guid);
            ev.source_name = std::string(data.source_name);
            ev.target_guid = std::string(data.dest_guid);
            ev.target_name = std::string(data.dest_name);
            ev.timestamp_ms = timestamp_ms;
            ev.spell_id = data.spell.spell_id;
            ev.spell_name = std::string(data.spell.spell_name);
            ev.extra_spell_id = data.extra_spell_id;
            ev.extra_spell_name = std::string(data.extra_spell_name);
            ev.extra_spell_school = data.extra_spell_school;
            ev.event_type = DispelInterruptEvent::EventType::Dispel;
            ev.aura_type = data.aura_type;
            target.dispelEvents.push_back(std::move(ev));
        }
    }
    else if (eventType == "SPELL_INTERRUPT") {
        isCombatEvent = true;
        if (tokens.size() >= parser::EventParser<parser::SpellInterruptTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellInterruptTag>::parse_and_return(tokens);
            if (!data.source_guid.empty() && !data.source_name.empty()) {
                guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
            }
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            if (data.spell.spell_id > 0 && !data.spell.spell_name.empty()) {
                spellIdToName_.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));
            }
            DispelInterruptEvent ev;
            ev.source_guid = std::string(data.source_guid);
            ev.source_name = std::string(data.source_name);
            ev.target_guid = std::string(data.dest_guid);
            ev.target_name = std::string(data.dest_name);
            ev.timestamp_ms = timestamp_ms;
            ev.spell_id = data.spell.spell_id;
            ev.spell_name = std::string(data.spell.spell_name);
            ev.extra_spell_id = data.extra_spell_id;
            ev.extra_spell_name = std::string(data.extra_spell_name);
            ev.extra_spell_school = data.extra_spell_school;
            ev.event_type = DispelInterruptEvent::EventType::Interrupt;
            ev.aura_type = AuraType::Unknown;
            target.dispelEvents.push_back(std::move(ev));
        }
    }
    else if (eventType == "SPELL_STOLEN") {
        isCombatEvent = true;
        if (tokens.size() >= parser::EventParser<parser::SpellStolenTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellStolenTag>::parse_and_return(tokens);
            if (!data.source_guid.empty() && !data.source_name.empty()) {
                guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
            }
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            if (data.spell.spell_id > 0 && !data.spell.spell_name.empty()) {
                spellIdToName_.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));
            }
            DispelInterruptEvent ev;
            ev.source_guid = std::string(data.source_guid);
            ev.source_name = std::string(data.source_name);
            ev.target_guid = std::string(data.dest_guid);
            ev.target_name = std::string(data.dest_name);
            ev.timestamp_ms = timestamp_ms;
            ev.spell_id = data.spell.spell_id;
            ev.spell_name = std::string(data.spell.spell_name);
            ev.extra_spell_id = data.extra_spell_id;
            ev.extra_spell_name = std::string(data.extra_spell_name);
            ev.extra_spell_school = data.extra_spell_school;
            ev.event_type = DispelInterruptEvent::EventType::Stolen;
            ev.aura_type = data.aura_type;
            target.dispelEvents.push_back(std::move(ev));
        }
    }
    // DEATHS
    else if (eventType == "UNIT_DIED" || eventType == "UNIT_DESTROYED"
          || eventType == "UNIT_DISSIPATES") {
        if (tokens.size() >= parser::EventParser<parser::UnitDiedTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::UnitDiedTag>::parse_and_return(tokens);
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            DeathEvent ev;
            ev.actor_guid = std::string(data.dest_guid);
            ev.actor_name = std::string(data.dest_name);
            ev.actor_type = awow::getActorTypeFromGuid(data.dest_guid);
            ev.timestamp_ms = timestamp_ms;
            ev.killing_spell_id = 0;
            ev.killing_spell_name = "";
            target.deathEvents.push_back(std::move(ev));
        }
    }
    else if (eventType == "SPELL_INSTAKILL") {
        if (tokens.size() >= parser::EventParser<parser::SpellInstakillTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellInstakillTag>::parse_and_return(tokens);
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            if (data.spell_id > 0 && !data.spell_name.empty()) {
                spellIdToName_.try_emplace(data.spell_id, std::string(data.spell_name));
            }
            DeathEvent ev;
            ev.actor_guid = std::string(data.dest_guid);
            ev.actor_name = std::string(data.dest_name);
            ev.actor_type = awow::getActorTypeFromGuid(data.dest_guid);
            ev.timestamp_ms = timestamp_ms;
            ev.killing_spell_id = data.spell_id;
            ev.killing_spell_name = std::string(data.spell_name);
            target.deathEvents.push_back(std::move(ev));
        }
    }
    // AURA BROKEN (for CC Breaks meter)
    else if (eventType == "SPELL_AURA_BROKEN") {
        if (tokens.size() >= parser::EventParser<parser::SpellAuraBrokenTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellAuraBrokenTag>::parse_and_return(tokens);
            if (!data.source_guid.empty() && !data.source_name.empty()) {
                guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
            }
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            if (data.spell.spell_id > 0 && !data.spell.spell_name.empty()) {
                spellIdToName_.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));
            }
            AuraEvent aura;
            aura.target_guid = std::string(data.dest_guid);
            aura.source_guid = std::string(data.source_guid);
            aura.spell_id = data.spell.spell_id;
            aura.spell_name = std::string(data.spell.spell_name);
            aura.aura_type = data.aura_type;
            aura.timestamp_ms = timestamp_ms;
            aura.event_type = AuraEventType::Broken;
            aura.stacks = 1;
            target.auraEvents.push_back(std::move(aura));
        }
    }
    else if (eventType == "SPELL_AURA_BROKEN_SPELL") {
        if (tokens.size() >= parser::EventParser<parser::SpellAuraBrokenSpellTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellAuraBrokenSpellTag>::parse_and_return(tokens);
            if (!data.source_guid.empty() && !data.source_name.empty()) {
                guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
            }
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            if (data.spell.spell_id > 0 && !data.spell.spell_name.empty()) {
                spellIdToName_.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));
            }
            AuraEvent aura;
            aura.target_guid = std::string(data.dest_guid);
            aura.source_guid = std::string(data.source_guid);
            aura.spell_id = data.spell.spell_id;
            aura.spell_name = std::string(data.spell.spell_name);
            aura.aura_type = data.aura_type;
            aura.timestamp_ms = timestamp_ms;
            aura.event_type = AuraEventType::BrokenSpell;
            aura.stacks = 1;
            aura.breaking_spell_id = data.extra_spell_id;
            aura.breaking_spell_name = std::string(data.extra_spell_name);
            target.auraEvents.push_back(std::move(aura));
        }
    }
    // SPELL_SUMMON is the authoritative source of pet -> owner ties
    // when the advanced combat log block skips owner_guid on the pet's
    // own damage events. Record the mapping keyed by summoned GUID
    // and let resolveOwnerGuidId() use it as a fallback.
    else if (eventType == "SPELL_SUMMON") {
        if (tokens.size() >= parser::EventParser<parser::SpellSummonTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellSummonTag>::parse_and_return(tokens);
            if (!data.source_guid.empty() && !data.source_name.empty()) {
                guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
            }
            if (!data.dest_guid.empty() && !data.dest_name.empty()) {
                guidToName_.try_emplace(std::string(data.dest_guid), std::string(data.dest_name));
            }
            // Only remember Player-owned pet/guardian summons. Skip
            // Player -> Player rows (buff proxies, Time Skip clones,
            // anything WoW ever routes through SPELL_SUMMON with a
            // Player target) so a player's own damage never gets
            // re-attributed to whoever summoned the buff on them.
            // Same guard the offline OutputWriter uses in spirit -
            // its map only ever seeds pet/guardian GUIDs since it
            // lives off SPELL_SUMMON's dest_guid being the summon.
            if (!data.dest_guid.empty() &&
                data.source_guid.starts_with("Player-") &&
                !data.dest_guid.starts_with("Player-")) {
                petToOwnerFromSummons_[std::string(data.dest_guid)] =
                    std::string(data.source_guid);
            }
        }
    }
    // SPELL_CAST_SUCCESS - phase rules of the "first cast of X" kind
    // resolve against the earliest completed cast of each spell, so
    // only the first sighting per spell id is kept. The cast-start tag
    // reads just the base + spell fields, which also covers logs
    // written without advanced logging enabled.
    else if (eventType == "SPELL_CAST_SUCCESS") {
        if (tokens.size() >= parser::EventParser<parser::SpellCastStartTag>::expected_token_count()) {
            auto data = parser::EventParser<parser::SpellCastStartTag>::parse_and_return(tokens);
            if (!data.source_guid.empty() && !data.source_name.empty()) {
                guidToName_.try_emplace(std::string(data.source_guid), std::string(data.source_name));
            }
            if (data.spell.spell_id > 0 && !data.spell.spell_name.empty()) {
                spellIdToName_.try_emplace(data.spell.spell_id, std::string(data.spell.spell_name));
            }
            if (data.spell.spell_id > 0) {
                auto [it, inserted] = firstCasts_.try_emplace(data.spell.spell_id);
                if (inserted) {
                    it->second.time_ms = timestamp_ms;
                    it->second.spell_name = std::string(data.spell.spell_name);
                    it->second.hostile_source = data.source_flags.isHostile();
                }
            }
        }
    }
    // EMOTE - boss speech lines. Phase rules of the "first emote
    // saying X" kind match against the cleaned text, and the phase
    // editor lists these as candidate split points. EMOTE lines skip
    // the usual flag fields (srcGuid at [3], text from [7] on) and
    // the body is unquoted, so it is rebuilt from the split tokens
    // exactly like the offline extractor does.
    else if (eventType == "EMOTE") {
        if (tokens.size() >= 8 && emoteEvents_.size() < kMaxEmotesPerSegment) {
            EmoteEvent emote;
            emote.timestamp_ms = timestamp_ms;
            emote.source_guid = std::string(tokens[3]);
            emote.source_name = std::string(tokens[4]);
            emote.text = awow::emote::emoteTextFromTokens(tokens);
            if (!emote.text.empty()) {
                guidToName_.try_emplace(emote.source_guid, emote.source_name);
                emoteEvents_.push_back(std::move(emote));
            }
        }
    }
    // COMBATANT_INFO - one line per player at every ENCOUNTER_START
    // (and M+ start). Only the GUID and spec id matter here; the spec
    // drives class-colored meter bars. The equipment/talent arrays at
    // the end of the line are deliberately not parsed.
    else if (eventType == "COMBATANT_INFO") {
        if (tokens.size() >= parser::EventParser<parser::CombatantInfoTag>::minimum_token_count()) {
            std::string_view playerGuid =
                parser::EventParser<parser::CombatantInfoTag>::extract_player_guid(tokens);
            uint16_t specId =
                parser::EventParser<parser::CombatantInfoTag>::extract_spec_id(tokens);
            if (!playerGuid.empty() && specId > 0) {
                guidToSpecId_[std::string(playerGuid)] = specId;
            }
        }
    }

    return isCombatEvent;
}
