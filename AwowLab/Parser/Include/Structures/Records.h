#pragma once
#include <cstdint>
#include <string>

// =============================================================================
// Data Records - Storage structures for parsed event data
// =============================================================================

// UnitRendering is used as an intermediate during parsing, but NOT stored per-actor.
// Position data is stored in ActorEvent/ActorEventCompact via EventDatabase.
struct UnitRendering {
    int32_t timestamp_ms;     // 4 bytes - relative log timestamp in ms (negative = before encounter)
    float x_loc;              // 4 bytes - raw world coordinate
    float y_loc;              // 4 bytes - raw world coordinate
    uint16_t radians;         // 2 bytes - orientation, radians * 10000 (0-62831)
    uint16_t map_id;          // 2 bytes - internal zone or instance identifier
};

struct ResourceStatusRecord {
    int32_t timestamp_ms;     // 4 bytes (negative = before encounter)
    uint64_t current_health;  // 8 bytes (64-bit for raid bosses with huge health pools)
    uint64_t max_health;      // 8 bytes
    uint32_t current_power;   // 4 bytes
    uint32_t max_power;       // 4 bytes
    int8_t powertype;         // 1 byte - negative values for health type (see the Spell.dbc powerType table)
    uint8_t padding[3];       // 3 bytes alignment
};

// Forward declaration for CombatEventType
enum class CombatEventType : uint8_t;

// Combat event record for damage/healing tracking
// Stored in per-actor tables for efficient time-windowed queries
// Uses interned string IDs for memory efficiency (4 bytes instead of ~40 bytes per GUID)
struct CombatRecord {
    int32_t timestamp_ms;            // 4 bytes - relative to encounter start (negative = before)
    uint32_t spell_id;               // 4 bytes - 0 for melee
    int64_t amount;                  // 8 bytes - raw damage/heal amount
    int64_t effective_amount;        // 8 bytes - amount - overkill/overheal
    int64_t absorbed;                // 8 bytes - absorbed component
    int64_t blocked;                 // 8 bytes - blocked component
    int64_t resisted;                // 8 bytes - resisted component
    uint32_t target_guid_id;         // 4 bytes - interned ID (use guidInterner().lookup())
    uint32_t owner_guid_id;          // 4 bytes - interned ID for pets/guardians (0 if not a pet)
    uint16_t flags;                  // 2 bytes - CombatEventFlags bitmask
    uint8_t spell_school;            // 1 byte - SpellSchool bitmask
    CombatEventType event_type;      // 1 byte
};

struct SpellCastRecord {
    int32_t timestamp_ms;         // 4 bytes - End/completion timestamp (SPELL_CAST_SUCCESS)
    int32_t start_timestamp_ms;   // 4 bytes - Start timestamp (SPELL_CAST_START), 0 if instant
    uint32_t spell_id;            // 4 bytes
    uint16_t haste_permille;      // 2 bytes - Haste in tenths of percent (300 = 30.0%)
    uint8_t cancelled;            // 1 byte  - True if cast was started but never completed
    uint8_t padding;              // 1 byte  - Alignment padding

    bool is_instant() const { return start_timestamp_ms == 0 || start_timestamp_ms == timestamp_ms; }
    int32_t cast_duration_ms() const { return is_instant() ? 0 : (timestamp_ms - start_timestamp_ms); }
    float haste_percent() const { return static_cast<float>(haste_permille) / 10.0f; }
};

// Cooldown reduction record for resource-based CD reductions
// Generated during log parsing when resource is spent (SPELL_CAST_SUCCESS with power cost)
// Used to track reductions to spell cooldowns (e.g., Demon Spikes CD reduced by Fury)
struct CooldownReductionRecord {
    uint32_t actor_guid_id;      // 4 bytes - Interned GUID ID of player who gained reduction
    uint32_t spell_id;           // 4 bytes - Spell whose CD was reduced
    int32_t timestamp_ms;        // 4 bytes - When reduction occurred
    int32_t reduction_ms;        // 4 bytes - How much CD was reduced (in milliseconds)
};  // Total: 16 bytes
