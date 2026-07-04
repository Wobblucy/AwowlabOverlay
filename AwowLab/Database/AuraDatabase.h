#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <algorithm>
#include "structures.h"

// Type of aura event for tracking state changes
enum class AuraEventType : uint8_t {
    Applied,        // SPELL_AURA_APPLIED - aura first applied
    Removed,        // SPELL_AURA_REMOVED - aura fully removed
    Refresh,        // SPELL_AURA_REFRESH - aura duration refreshed
    AppliedDose,    // SPELL_AURA_APPLIED_DOSE - stack added
    RemovedDose,    // SPELL_AURA_REMOVED_DOSE - stack removed (aura remains)
    Broken,         // SPELL_AURA_BROKEN - aura broken (e.g., CC broken)
    BrokenSpell     // SPELL_AURA_BROKEN_SPELL - aura broken by specific spell
};

// Raw aura event extracted from combat log
struct AuraEvent {
    std::string target_guid;      // Who has the aura
    std::string source_guid;      // Who applied it
    uint32_t spell_id;
    std::string spell_name;
    AuraType aura_type;           // BUFF or DEBUFF
    int32_t timestamp_ms;         // Relative to encounter start (negative = before encounter)
    AuraEventType event_type;
    uint8_t stacks;               // Stack count (for dose events, default 1)
    // For BrokenSpell events: the spell that broke the aura
    uint32_t breaking_spell_id = 0;
    std::string breaking_spell_name;
};

// CC break event for tracking who broke crowd control
struct CCBreakEvent {
    std::string breaker_guid;     // Who broke the CC (source of the breaking spell)
    std::string target_guid;      // Who had the CC
    uint32_t cc_spell_id;         // The CC spell that was broken
    std::string cc_spell_name;
    uint32_t breaking_spell_id;   // The spell that broke it (0 if not BROKEN_SPELL)
    std::string breaking_spell_name;
    int32_t timestamp_ms;
};

// Per-actor CC break statistics for meter display
struct ActorCCBreakStats {
    std::string actor_guid;
    uint32_t break_count = 0;
    float percent_of_total = 0.0f;

    struct BreakDetail {
        uint32_t cc_spell_id;
        std::string cc_spell_name;
        uint32_t count;
    };
    std::vector<BreakDetail> breakdown;
};

// Represents a stack change event in the dose history timeline
struct DoseChange {
    int32_t timestamp_ms;         // When the stack count changed
    uint8_t stack_count;          // New stack count after this change
};

// Represents a single aura instance from application to removal
// Duration is calculated as removed_at_ms - applied_at_ms
struct AuraInstance {
    std::string target_guid;      // Who has the aura
    std::string source_guid;      // Who applied it
    uint32_t spell_id;
    std::string spell_name;       // Cached for display
    AuraType aura_type;           // BUFF or DEBUFF
    int32_t applied_at_ms;        // Start timestamp (negative = before encounter)
    int32_t removed_at_ms;        // End timestamp (INT32_MAX if still active)
    uint8_t max_stacks;           // Peak stack count during this instance
    uint8_t current_stacks;       // Current (or final) stack count
    bool start_inferred = false;  // true if applied_at_ms was backdated from first-seen timestamp

    // Timeline of stack changes for time-accurate stack queries
    // Sorted by timestamp_ms ascending. First entry is always at applied_at_ms with initial stack count.
    std::vector<DoseChange> doseHistory;

    // Calculate duration in milliseconds (0 if still active)
    int32_t getDurationMs() const {
        if (removed_at_ms == INT32_MAX) return 0;
        return removed_at_ms - applied_at_ms;
    }

    // Check if aura was active at given timestamp
    bool isActiveAt(int32_t timestamp_ms) const {
        return applied_at_ms <= timestamp_ms && timestamp_ms < removed_at_ms;
    }

    // Get stack count at a specific timestamp using dose history
    // Uses binary search for efficiency
    uint8_t getStackCountAt(int32_t timestamp_ms) const {
        if (doseHistory.empty()) {
            return current_stacks > 0 ? current_stacks : 1;
        }
        // Binary search for the last dose change <= timestamp_ms
        auto it = std::upper_bound(doseHistory.begin(), doseHistory.end(), timestamp_ms,
            [](int32_t ts, const DoseChange& dc) { return ts < dc.timestamp_ms; });
        if (it == doseHistory.begin()) {
            // Before first dose change, return initial stacks (first entry)
            return doseHistory.front().stack_count;
        }
        --it;  // Move to the last entry <= timestamp_ms
        return it->stack_count;
    }
};

// AuraDatabase - Stores and queries buff/debuff tracking data
// Provides efficient time-based queries for active auras
class AuraDatabase {
public:
    AuraDatabase() = default;
    ~AuraDatabase() = default;

    // Load from extracted events (takes ownership via move)
    // firstSeenTimestamps: GUID -> min timestamp, used to backdate removal-only buffs
    void loadFromEvents(
        std::vector<AuraEvent>&& events,
        const std::unordered_map<std::string, int32_t>& firstSeenTimestamps = {}
    );

    // Clear all data
    void clear();

    // Check if database has data
    bool empty() const { return targetAuras_.empty(); }

    // Get number of unique targets with auras
    size_t targetCount() const { return targetAuras_.size(); }

    // Get time bounds (can be negative for pre-encounter events)
    int32_t getMinTimestamp() const { return min_timestamp_; }
    int32_t getMaxTimestamp() const { return max_timestamp_; }

    // ========== Query Methods ==========

    // Get all active auras on a target at a specific time
    std::vector<const AuraInstance*> getActiveAurasOnTarget(
        std::string_view target_guid,
        int32_t timestamp_ms
    ) const;

    // Get all active auras from a source at a specific time
    std::vector<const AuraInstance*> getActiveAurasFromSource(
        const std::string& source_guid,
        int32_t timestamp_ms
    ) const;

    // Get specific aura instance (most recent active at timestamp)
    const AuraInstance* getAuraInstance(
        const std::string& target_guid,
        const std::string& source_guid,
        uint32_t spell_id,
        int32_t timestamp_ms
    ) const;

    // Get all aura instances for a target (full history)
    std::vector<const AuraInstance*> getAllAurasOnTarget(
        const std::string& target_guid
    ) const;

    // Get all aura instances from a source (full history)
    std::vector<const AuraInstance*> getAllAurasFromSource(
        const std::string& source_guid
    ) const;

    // Get all target auras (for iteration)
    const std::unordered_map<std::string, std::vector<AuraInstance>>& getAllTargetAuras() const {
        return targetAuras_;
    }

    // Get stack count for a specific aura at a specific time
    // Returns 0 if aura not active
    uint8_t getStackCount(
        const std::string& target_guid,
        const std::string& source_guid,
        uint32_t spell_id,
        int32_t timestamp_ms
    ) const;

    // ========== CC Break Query Methods ==========

    // Check if we have CC break data
    bool hasCCBreaks() const { return !ccBreaks_.empty(); }

    // Get total CC break count in time range
    size_t getCCBreakCount(int32_t start_time_ms, int32_t end_time_ms) const;

    // Get actors ranked by CC breaks in time range
    std::vector<ActorCCBreakStats> getRankedByCCBreaks(
        int32_t start_time_ms,
        int32_t end_time_ms,
        size_t max_results = 40
    ) const;

    // Get all CC break events (for list display)
    const std::vector<CCBreakEvent>& getAllCCBreaks() const { return ccBreaks_; }

#ifndef NDEBUG
    // Iterate over all aura instances (for debug dumping)
    template<typename Func>
    void forEachAuraInstance(Func&& func) const {
        for (const auto& [target_guid, instances] : targetAuras_) {
            for (const auto& instance : instances) {
                func(instance);
            }
        }
    }

    // Get total instance count (for debug stats)
    size_t instanceCount() const {
        size_t count = 0;
        for (const auto& [_, instances] : targetAuras_) {
            count += instances.size();
        }
        return count;
    }

    // Get source index for debug dumping (source_guid -> aura instances they applied)
    const std::unordered_map<std::string, std::vector<AuraInstance*>>& getSourceIndex() const { return sourceIndex_; }
#endif

private:
    // Primary storage: target_guid -> vector of AuraInstance (sorted by applied_at_ms)
    std::unordered_map<std::string, std::vector<AuraInstance>> targetAuras_;

    // Secondary index: source_guid -> vector of pointers to instances
    std::unordered_map<std::string, std::vector<AuraInstance*>> sourceIndex_;

    // CC break events (sorted by timestamp)
    std::vector<CCBreakEvent> ccBreaks_;

    int32_t min_timestamp_ = INT32_MAX;
    int32_t max_timestamp_ = INT32_MIN;

    // Build instances from raw events
    void buildAuraInstances(
        std::vector<AuraEvent>& events,
        const std::unordered_map<std::string, int32_t>& firstSeenTimestamps
    );

    // Build secondary source index after instances are created
    void buildSourceIndex();

    // Extract CC break events from raw events
    void extractCCBreaks(const std::vector<AuraEvent>& events);
};
