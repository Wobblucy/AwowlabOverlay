#pragma once
#include <vector>
#include "../structures.h"

struct ActorTable {
	// Note: rendering_table was removed - position data is in EventDatabase (ActorEvent/ActorEventCompact)
	std::vector<SpellCastRecord> spell_cast_table;      // GCD history for each actor
	std::vector<ResourceStatusRecord> resource_status_table;  // Health/power tracking per actor

	// Combat metrics tables - sorted by timestamp for binary search
	std::vector<CombatRecord> damage_dealt_table;       // Damage this actor dealt to others
	std::vector<CombatRecord> healing_done_table;       // Healing this actor did to others
};
