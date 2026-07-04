#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <imgui.h>
#include "Database/CombatDatabase.h"
#include "UI/SpellGroupSettings.h"

namespace awow { class ISpellIconRenderer; }

// Grouped spell display structure
struct GroupedSpellRow {
    enum class Type { GroupHeader, Spell };
    Type type = Type::Spell;
    uint32_t group_id = 0;          // For headers: the group ID; for spells: parent group (0 if ungrouped)
    const SpellCombatStats* spell = nullptr;  // nullptr for headers
    int64_t group_total = 0;        // For headers: sum of all spells in group
    std::string group_name;         // For headers: the group name
};

// Renders the spell breakdown table with grouping support
// Handles spell grouping drag-drop, blacklisting, and row rendering
class BreakdownSpellTableRenderer {
public:
    BreakdownSpellTableRenderer() = default;

    // Reset state for new actor
    void reset();

    // Set the actor stats to display
    void setActorStats(const ActorCombatStats* stats, CombatMetricType metricType);

    // Set current spec ID for spell grouping
    void setSpecId(uint16_t specId) { currentSpecId_ = specId; }

    // Get spell group settings (for blacklist filtering in graph)
    spell_grouping::SpellGroupSettings& getGroupSettings() { return spellGroupSettings_; }
    const spell_grouping::SpellGroupSettings& getGroupSettings() const { return spellGroupSettings_; }

    // Load settings from persistent storage
    void loadSettings() { spellGroupSettings_.loadFromSettings(); }

    // Overlay-mode spell-name fallback. Keyed by spell ID; consulted
    // before SPELL_NAME so drill-downs read correctly in the client's
    // locale without a SpellDataTable binary loaded. Pass nullptr in
    // the main-app path (the default).
    void setSpellNameFallback(const std::unordered_map<uint32_t, std::string>* fallback) {
        spellNameFallback_ = fallback;
    }

    // Build grouped spell rows from current actor stats
    void buildGroupedSpellRows();

    // Render the spell table
    // Returns the selected spell index, or -1 if none
    void render(awow::ISpellIconRenderer* iconLoader, const std::string& actorGuid,
                int& selectedSpellIndex, bool& needsRefresh);

    // Get the top spell IDs for consistent graph coloring
    const std::vector<uint32_t>& getTopSpellIds() const { return topSpellIds_; }

    // Check if rebuild is needed (for deferred rebuild)
    bool needsRebuild() const { return needsGroupRebuild_; }
    void setNeedsRebuild(bool needs) { needsGroupRebuild_ = needs; }

private:
    // Current actor stats (non-owning)
    const ActorCombatStats* actorStats_ = nullptr;
    CombatMetricType metricType_ = CombatMetricType::DamageDealt;

    // Spell grouping
    spell_grouping::SpellGroupSettings spellGroupSettings_;
    uint16_t currentSpecId_ = 0;
    bool isEditingGroupName_ = false;
    uint32_t editingGroupId_ = 0;
    char groupNameBuffer_[64] = {};

    // Grouped spell rows
    std::vector<GroupedSpellRow> groupedSpellRows_;
    std::vector<const SpellCombatStats*> blacklistedSpellRows_;
    bool needsGroupRebuild_ = false;

    // Top N spell IDs for consistent coloring
    std::vector<uint32_t> topSpellIds_;

    // Optional overlay-mode name lookup, set by setSpellNameFallback.
    const std::unordered_map<uint32_t, std::string>* spellNameFallback_ = nullptr;

    // Special actor GUID for blacklist view
    static constexpr const char* BLACKLIST_ACTOR_GUID = "__BLACKLISTED__";

    // Render individual components
    void renderGroupingControls(bool& needsRefresh);
    void renderGroupHeader(GroupedSpellRow& row, awow::ISpellIconRenderer* iconLoader, bool& needsRefresh);
    void renderSpellRow(GroupedSpellRow& row, awow::ISpellIconRenderer* iconLoader,
                        size_t displayIndex, int& selectedSpellIndex,
                        const std::string& actorGuid, bool& needsRefresh);
    void renderBlacklistedSection(awow::ISpellIconRenderer* iconLoader, bool& needsRefresh);

    // Get color for a spell in the legend swatch
    ImU32 getSpellColor(uint32_t spellId) const;
};
