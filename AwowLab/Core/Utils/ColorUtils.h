#pragma once

#include <string_view>
#include <cstdint>
#include <cmath>
#include <functional>
#include "Database/EventTypes.h"

namespace awow {

/**
 * @brief Convert HSV color to RGB.
 *
 * @param h Hue (0.0 - 1.0)
 * @param s Saturation (0.0 - 1.0)
 * @param v Value/brightness (0.0 - 1.0)
 * @return ActorColor RGB color
 */
::ActorColor hsvToRgb(float h, float s, float v);

/**
 * @brief Parse spawn index from creature/vehicle GUID for color differentiation.
 *
 * Extracts and hashes the spawn UID portion of a WoW GUID to provide
 * unique coloring for different spawns of the same creature type.
 *
 * @param guid WoW GUID string (e.g., "Creature-0-1234-5678-9ABC-DEF0")
 * @return uint32_t Spawn index value for color seeding
 */
uint32_t parseSpawnIndex(std::string_view guid);

/**
 * @brief Generate a deterministic color for non-player actors.
 *
 * Uses actor type, unit flags, and GUID hash to generate consistent
 * colors that differentiate hostiles, friendlies, and neutrals.
 *
 * @param type Actor type (Creature, Vehicle, Pet, Player, Unknown)
 * @param flags Unit flags for reaction detection (hostile/friendly/neutral)
 * @param guid Actor's GUID for hash-based color variation
 * @return ActorColor Generated RGB color
 */
::ActorColor generateActorColor(::ActorType type, const ::UnitFlags& flags, std::string_view guid);

/**
 * @brief Determine actor type from GUID prefix.
 *
 * @param guid WoW GUID string
 * @return ActorType Detected type (Player, Creature, Vehicle, Pet, Unknown)
 */
::ActorType getActorTypeFromGuid(std::string_view guid);

} // namespace awow
