#pragma once
#include <string>
#include <string_view>
#include <deque>
#include <unordered_map>
#include <cstdint>

// String interning pool to eliminate duplicate string storage
// Used for GUIDs, spell names, and other repeated strings
//
// Example:
//   auto& interner = guidInterner();
//   uint32_t id = interner.intern("Player-3657-0A5F32EA");
//   std::string_view str = interner.lookup(id);
//
class StringInterner {
public:
    using Id = uint32_t;
    static constexpr Id INVALID = 0;

    StringInterner();
    ~StringInterner() = default;

    // Non-copyable, movable
    StringInterner(const StringInterner&) = delete;
    StringInterner& operator=(const StringInterner&) = delete;
    StringInterner(StringInterner&&) = default;
    StringInterner& operator=(StringInterner&&) = default;

    // Intern a string - returns existing ID if already interned, or creates new
    // Thread-safety: NOT thread-safe, call from single thread only
    Id intern(std::string_view str);

    // Look up the ID for an already-interned string without creating
    // one - returns INVALID if the string was never interned. Safe for
    // read-only callers (UI queries) that must not grow the pool.
    Id find(std::string_view str) const;

    // Lookup string by ID - returns empty string_view if invalid
    std::string_view lookup(Id id) const;

    // Check if ID is valid
    bool isValid(Id id) const;

    // Get number of unique strings interned
    size_t size() const { return strings_.size(); }

    // Get approximate memory usage in bytes
    size_t memoryUsage() const;

    // Clear all interned strings (call when loading new encounter)
    void clear();

private:
    // ID 0 is reserved as INVALID, so strings_[0] is empty placeholder
    // Using deque instead of vector: deque doesn't invalidate references on push_back
    std::deque<std::string> strings_;

    // Map from string content to ID for O(1) lookup during interning
    // string_view keys point into strings_ deque - safe because deque preserves references
    std::unordered_map<std::string_view, Id> index_;
};

// Global interner instances (session-scoped)
// These are cleared when loading a new encounter

// For GUIDs like "Player-3657-0A5F32EA", "Creature-0-3657-..."
StringInterner& guidInterner();

// For spell names (optional, for UI display)
StringInterner& spellNameInterner();

// For actor display names
StringInterner& actorNameInterner();

// Clear all global interners (call when loading new encounter)
void clearAllInterners();
