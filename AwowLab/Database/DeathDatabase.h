#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <unordered_map>
#include <algorithm>
#include "EventTypes.h"

// Forward declaration
class EventDatabase;

// Database for tracking unit death and resurrection states
// Used to hide dead units from rendering and show player deaths in UI
class DeathDatabase {
public:
    // Load death and resurrection events
    void loadEvents(const std::vector<DeathEvent>& deaths,
                    const std::vector<ResurrectEvent>& resurrects) {
        storeEvents(deaths, resurrects);
        sortAndIndex();
    }

    // Load death and resurrection events, then infer implicit resurrections
    // when actors appear in position data after dying (handles release in
    // M+/Challenge Mode where there's no SPELL_RESURRECT event).
    // Defined in DeathDatabase_Positions.cpp so builds without position
    // tracking can use the overload above without linking EventDatabase.
    void loadEvents(const std::vector<DeathEvent>& deaths,
                    const std::vector<ResurrectEvent>& resurrects,
                    const EventDatabase* eventDb);

    // Check if actor should be hidden from rendering
    // Returns true if actor died more than hideDelayMs ago and hasn't been resurrected
    bool shouldHide(std::string_view guid, int32_t currentTime, int32_t hideDelayMs = 500) const {
        auto it = actorLifeEvents_.find(std::string(guid));
        if (it == actorLifeEvents_.end()) {
            return false;  // No death events for this actor
        }

        const auto& events = it->second;
        if (events.empty()) {
            return false;
        }

        // Binary search for the last event at or before currentTime
        auto upper = std::upper_bound(events.begin(), events.end(), currentTime,
            [](int32_t ts, const std::pair<int32_t, bool>& event) {
                return ts < event.first;
            });

        if (upper == events.begin()) {
            return false;  // No events before current time
        }

        const auto& lastEvent = *(upper - 1);
        bool isDead = lastEvent.second;  // true = death, false = resurrect
        int32_t eventTime = lastEvent.first;

        // Hide if dead and delay has passed
        if (isDead && currentTime >= eventTime + hideDelayMs) {
            return true;
        }

        return false;
    }

    // Get all deaths (for UI panel display)
    const std::vector<DeathEvent>& getAllDeaths() const {
        return allDeaths_;
    }

    // Get player deaths only (for UI panel - filtered view)
    std::vector<const DeathEvent*> getPlayerDeaths() const {
        std::vector<const DeathEvent*> playerDeaths;
        for (const auto& death : allDeaths_) {
            if (death.actor_type == ActorType::Player) {
                playerDeaths.push_back(&death);
            }
        }
        return playerDeaths;
    }

    // Get player deaths in time range (for encounter filtering)
    std::vector<const DeathEvent*> getPlayerDeathsInRange(int32_t start_time_ms, int32_t end_time_ms) const {
        std::vector<const DeathEvent*> result;

        // Binary search for start position (allDeaths_ is sorted by timestamp)
        auto startIt = std::lower_bound(allDeaths_.begin(), allDeaths_.end(), start_time_ms,
            [](const DeathEvent& e, int32_t time) {
                return e.timestamp_ms < time;
            });

        // Iterate from start position to end of range, filtering for players
        for (auto it = startIt; it != allDeaths_.end() && it->timestamp_ms <= end_time_ms; ++it) {
            if (it->actor_type == ActorType::Player) {
                result.push_back(&(*it));
            }
        }

        return result;
    }

    // Timestamp bounds for time filtering
    int32_t getMinTimestamp() const { return min_timestamp_; }
    int32_t getMaxTimestamp() const { return max_timestamp_; }

    // Get resurrection count by resurrecter (who rezzed whom)
    // Returns vector of (resurrecter_guid, count) sorted by count descending
    const std::vector<ResurrectEvent>& getAllResurrections() const {
        return allResurrects_;
    }

    // Check if we have resurrection data
    bool hasResurrections() const {
        return !allResurrects_.empty();
    }

private:
    // Copy the raw events and build the per-actor life event timelines
    // (unsorted). Each timeline entry is (timestamp, isDeath).
    void storeEvents(const std::vector<DeathEvent>& deaths,
                     const std::vector<ResurrectEvent>& resurrects) {
        allDeaths_ = deaths;
        allResurrects_ = resurrects;
        actorLifeEvents_.clear();

        for (const auto& death : deaths) {
            actorLifeEvents_[death.actor_guid].emplace_back(death.timestamp_ms, true);
        }
        for (const auto& resurrect : resurrects) {
            actorLifeEvents_[resurrect.actor_guid].emplace_back(resurrect.timestamp_ms, false);
        }
    }

    // Sort the timelines and death list, then record the timestamp bounds
    void sortAndIndex() {
        // Sort each actor's events by timestamp
        for (auto& [guid, events] : actorLifeEvents_) {
            std::sort(events.begin(), events.end(),
                [](const std::pair<int32_t, bool>& a, const std::pair<int32_t, bool>& b) {
                    return a.first < b.first;
                });
        }

        // Sort all deaths by timestamp for UI display
        std::sort(allDeaths_.begin(), allDeaths_.end(),
            [](const DeathEvent& a, const DeathEvent& b) {
                return a.timestamp_ms < b.timestamp_ms;
            });

        // Track min/max timestamps for time filtering
        if (!allDeaths_.empty()) {
            min_timestamp_ = allDeaths_.front().timestamp_ms;
            max_timestamp_ = allDeaths_.back().timestamp_ms;
        } else {
            min_timestamp_ = 0;
            max_timestamp_ = 0;
        }
    }

    // Infer resurrections from position events
    // For each actor that died, find if they have position events after death
    // If so, their first position event after death is an implicit resurrect
    void inferResurrectionsFromPositionEvents(const EventDatabase* eventDb);

    // Per-actor timeline: vector of (timestamp, isDeath) pairs, sorted by timestamp
    std::unordered_map<std::string, std::vector<std::pair<int32_t, bool>>> actorLifeEvents_;

    // All death events sorted by timestamp (for UI display)
    std::vector<DeathEvent> allDeaths_;

    // All resurrection events (for resurrection tracking)
    std::vector<ResurrectEvent> allResurrects_;

    // Timestamp bounds for time filtering
    int32_t min_timestamp_ = 0;
    int32_t max_timestamp_ = 0;
};
