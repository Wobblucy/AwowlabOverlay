#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

// Per-mob damage weighting. Users assign a weight to an NPC id and
// damage done to that mob counts at that fraction in the damage-done
// meters - weight an unimportant add at 10% and padding on it
// contributes a tenth of its raw number, so the meter shows who is
// actually pushing the dungeon forward. Damage taken and healing are
// never weighted; only the contribution view changes.
//
// Weights are keyed by NPC id (the 6th field of a Creature/Vehicle
// GUID), which is stable across locales and across every spawn of
// the same mob.
//
// Persistence stays with the caller: the settings panel round-trips
// toJson/fromJson through SettingsCache. This class deliberately has
// no other dependencies so the database layer can read it from any
// target.
class MobWeightSettings {
public:
    static MobWeightSettings& instance();

    bool enabled = true;

    // npc id -> weight multiplier in [0, 1]; missing means full credit
    std::unordered_map<uint32_t, float> weights;

    // Weight for an npc id: 1.0 when unlisted or when weighting is off
    float weightFor(uint32_t npcId) const;

    // Pull the npc id out of a unit GUID
    // ("Creature-0-3018-2810-26542-242587-000232672E" -> 242587).
    // Returns 0 for players and anything else without an npc id.
    static uint32_t npcIdFromGuid(std::string_view guid);

    std::string toJson() const;
    void fromJson(const std::string& json);

private:
    MobWeightSettings() = default;
};
