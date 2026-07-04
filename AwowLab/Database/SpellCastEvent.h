#pragma once
#include <string>
#include <cstdint>

// Spell cast event for database storage.
// Lives in its own header so EventTypes.h (which stores these in
// ExtractedData) doesn't have to pull in the full SpellCastDatabase.
struct SpellCastEvent {
    std::string actor_guid;
    std::string target_guid;      // Target of the spell (dest_guid from combat log)
    uint32_t spell_id;
    int32_t start_timestamp_ms;   // When cast began (SPELL_CAST_START), 0 if instant (negative = before encounter)
    int32_t end_timestamp_ms;     // When cast completed (SPELL_CAST_SUCCESS) or cancelled
    uint16_t haste_permille;      // Haste at time of cast in permille (300 = 30.0%)
    bool cancelled;               // True if cast was started but never completed
    int32_t cooldown_ms = 0;         // Cooldown duration in ms (from SpellGCDDatabase)
    int32_t cooldown_ready_ms = 0;   // When cooldown will be ready (end_timestamp + cooldown)
    int32_t power_cost = 0;          // Resource cost of the spell (drives resource-based CD reductions)
    int32_t gcd_ms = 0;              // GCD duration in ms (pre-calculated with haste)

    // For backward compatibility and convenience
    int32_t timestamp_ms() const { return end_timestamp_ms; }
    bool is_instant() const { return start_timestamp_ms == 0 || start_timestamp_ms == end_timestamp_ms; }
    int32_t cast_duration_ms() const {
        return is_instant() ? 0 : (end_timestamp_ms - start_timestamp_ms);
    }

    // Convert haste permille to percentage (e.g., 305 -> 30.5%)
    float haste_percent() const {
        return static_cast<float>(haste_permille) / 10.0f;
    }

    // Check if spell is on cooldown at a given time
    bool isOnCooldown(int32_t current_time_ms) const {
        return cooldown_ms > 0 && current_time_ms < cooldown_ready_ms;
    }

    // Get remaining cooldown at a given time (0 if ready)
    int32_t getRemainingCooldown(int32_t current_time_ms) const {
        if (!isOnCooldown(current_time_ms)) return 0;
        return cooldown_ready_ms - current_time_ms;
    }
};
