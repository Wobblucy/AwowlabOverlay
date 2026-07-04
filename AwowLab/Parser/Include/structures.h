#pragma once

// =============================================================================
// structures.h - Umbrella header for backward compatibility
// =============================================================================
// This header includes all structure definitions from their organized submodules.
// New code should prefer importing specific headers from Structures/ directly.
// =============================================================================

// Unit flags, affiliations, reactions, etc.
#include "Structures/Flags.h"

// Event type enumeration, power types
#include "Structures/EventTypes.h"

// Combat event types, spell schools, miss types, aura types
#include "Structures/CombatTypes.h"

// Token indices for parsing combat log lines
#include "Structures/TokenIndex.h"

// Storage records: ResourceStatusRecord, CombatRecord, etc. (UnitRendering is parsing-only)
#include "Structures/Records.h"

// Parsed event data structures: DamageEventData, HealEventData, etc.
#include "Structures/ParsedEvents.h"
