#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace spell_grouping {

// A single spell group definition
struct SpellGroup {
    uint32_t group_id = 0;
    std::string name;
    std::vector<uint32_t> spell_ids;
    bool collapsed = false;
};

// All groups for a single spec
struct SpecSpellGroups {
    uint16_t spec_id = 0;
    std::vector<SpellGroup> groups;
    uint32_t next_group_id = 1;

    // Find group containing a spell, returns nullptr if ungrouped
    SpellGroup* findGroupContainingSpell(uint32_t spell_id);
    const SpellGroup* findGroupContainingSpell(uint32_t spell_id) const;

    // Find group by ID
    SpellGroup* findGroupById(uint32_t group_id);
    const SpellGroup* findGroupById(uint32_t group_id) const;

    // Group management
    SpellGroup& createGroup(const std::string& name);
    void deleteGroup(uint32_t group_id);
    bool addSpellToGroup(uint32_t group_id, uint32_t spell_id);
    bool removeSpellFromGroup(uint32_t spell_id);
    void moveSpellToGroup(uint32_t spell_id, uint32_t target_group_id);
};

// Complete settings for all specs
class SpellGroupSettings {
public:
    SpellGroupSettings() = default;

    // Get or create groups for a spec
    SpecSpellGroups& getOrCreateSpecGroups(uint16_t spec_id);
    const SpecSpellGroups* getSpecGroups(uint16_t spec_id) const;

    // Check if a spell is grouped for a spec
    bool isSpellGrouped(uint16_t spec_id, uint32_t spell_id) const;

    // Get all grouped spell IDs for a spec (for quick lookup)
    std::unordered_set<uint32_t> getGroupedSpellIds(uint16_t spec_id) const;

    // Global spell blacklist (applies to all specs/actors)
    bool isSpellBlacklisted(uint32_t spell_id) const;
    void blacklistSpell(uint32_t spell_id);
    void unblacklistSpell(uint32_t spell_id);
    const std::unordered_set<uint32_t>& getBlacklistedSpells() const { return blacklistedSpells_; }

    // Serialization
    std::string serializeToJson() const;
    bool deserializeFromJson(const std::string& json);

    // Load/save via SettingsCache
    void loadFromSettings();
    void saveToSettings();

private:
    std::unordered_map<uint16_t, SpecSpellGroups> specGroups_;
    std::unordered_set<uint32_t> blacklistedSpells_;
};

} // namespace spell_grouping
