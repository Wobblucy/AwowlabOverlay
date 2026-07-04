#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <optional>
#include <unordered_map>
#include "EventTypes.h"
#include "Actor/ActorMap.h"

// Database for efficient resource (health/power) queries
// Uses step-wise interpolation (returns last known value at or before timestamp)
// Operates as a non-owning facade over ActorMap's resource_status_table
class ResourceDatabase {
private:
    // Non-owning pointer to ActorMap (facade mode)
    const ActorMap* actorMap_ = nullptr;

    int32_t min_timestamp_ = 0;
    int32_t max_timestamp_ = 0;

public:
    // Load from ActorMap (facade mode - no data copying)
    void loadFromActorMap(const ActorMap* actorMap) {
        actorMap_ = actorMap;

        if (!actorMap_ || actorMap_->empty()) {
            min_timestamp_ = 0;
            max_timestamp_ = 0;
            return;
        }

        // Calculate min/max timestamps by scanning all actors' resource tables
        bool first = true;
        for (const auto& [guid, table] : *actorMap_) {
            if (!table.resource_status_table.empty()) {
                // Tables are already sorted by timestamp
                int32_t actorMin = table.resource_status_table.front().timestamp_ms;
                int32_t actorMax = table.resource_status_table.back().timestamp_ms;

                if (first) {
                    min_timestamp_ = actorMin;
                    max_timestamp_ = actorMax;
                    first = false;
                } else {
                    min_timestamp_ = (std::min)(min_timestamp_, actorMin);
                    max_timestamp_ = (std::max)(max_timestamp_, actorMax);
                }
            }
        }
    }

    // Get latest resource state at or before given timestamp (step-wise interpolation)
    std::optional<ResourceEvent> getResourceAt(const std::string& actor_guid,
                                                int32_t timestamp_ms) const {
        if (!actorMap_) return std::nullopt;

        // Read-only boundary: find() never grows the interner pool.
        StringInterner::Id actor_id = guidInterner().find(actor_guid);
        if (actor_id == StringInterner::INVALID) return std::nullopt;

        auto it = actorMap_->find(actor_id);
        if (it == actorMap_->end()) return std::nullopt;

        const auto& records = it->second.resource_status_table;
        if (records.empty()) return std::nullopt;

        // Binary search for last record at or before timestamp
        auto upper = std::upper_bound(records.begin(), records.end(), timestamp_ms,
            [](int32_t ts, const ResourceStatusRecord& r) {
                return ts < r.timestamp_ms;
            });

        if (upper == records.begin()) return std::nullopt;

        // Convert ResourceStatusRecord to ResourceEvent for API compatibility
        const auto& record = *(upper - 1);
        ResourceEvent event;
        event.actor_guid = actor_guid;
        event.timestamp_ms = record.timestamp_ms;
        event.current_health = record.current_health;
        event.max_health = record.max_health;
        event.current_power = record.current_power;
        event.max_power = record.max_power;
        event.power_type = record.powertype;

        return event;
    }

    // Batch query for multiple actors at once (for frame cache efficiency)
    std::unordered_map<std::string, ResourceEvent> getResourcesAt(
        const std::vector<std::string>& guids,
        int32_t timestamp_ms) const {

        std::unordered_map<std::string, ResourceEvent> result;
        result.reserve(guids.size());

        for (const auto& guid : guids) {
            auto resource = getResourceAt(guid, timestamp_ms);
            if (resource.has_value()) {
                result.emplace(guid, std::move(resource.value()));
            }
        }

        return result;
    }

    // Get all resource events for an actor (for detailed view/graphs)
    std::vector<ResourceEvent> getAllResourcesForActor(const std::string& actor_guid) const {
        std::vector<ResourceEvent> result;

        if (!actorMap_) return result;

        StringInterner::Id actor_id = guidInterner().find(actor_guid);
        if (actor_id == StringInterner::INVALID) return result;

        auto it = actorMap_->find(actor_id);
        if (it == actorMap_->end()) return result;

        const auto& records = it->second.resource_status_table;
        result.reserve(records.size());

        for (const auto& record : records) {
            ResourceEvent event;
            event.actor_guid = actor_guid;
            event.timestamp_ms = record.timestamp_ms;
            event.current_health = record.current_health;
            event.max_health = record.max_health;
            event.current_power = record.current_power;
            event.max_power = record.max_power;
            event.power_type = record.powertype;
            result.push_back(std::move(event));
        }

        return result;
    }

    int32_t getMinTimestamp() const { return min_timestamp_; }
    int32_t getMaxTimestamp() const { return max_timestamp_; }

    bool empty() const {
        if (!actorMap_) return true;
        for (const auto& [guid, table] : *actorMap_) {
            if (!table.resource_status_table.empty()) return false;
        }
        return true;
    }

    size_t size() const {
        size_t total = 0;
        if (actorMap_) {
            for (const auto& [guid, table] : *actorMap_) {
                total += table.resource_status_table.size();
            }
        }
        return total;
    }

    size_t actorCount() const {
        size_t count = 0;
        if (actorMap_) {
            for (const auto& [guid, table] : *actorMap_) {
                if (!table.resource_status_table.empty()) count++;
            }
        }
        return count;
    }
};
