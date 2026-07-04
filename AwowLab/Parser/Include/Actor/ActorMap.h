#pragma once
#include <unordered_map>
#include "ActorTable.h"
#include "../structures.h"
#include "../../../Core/StringInterner.h"

// Keyed by interned GUID id rather than the GUID string itself: hashing
// a 4-byte int beats hashing a ~60-byte GUID on every event insert and
// lookup. Resolve ids back to GUID strings with guidInterner().lookup().
using ActorMap = std::unordered_map<StringInterner::Id, ActorTable>;