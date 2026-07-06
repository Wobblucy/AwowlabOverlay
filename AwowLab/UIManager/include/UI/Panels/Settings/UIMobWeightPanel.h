#pragma once

#include <imgui.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class CombatDatabase;

// Settings panel for per-mob damage weighting
// Lists the creatures of the loaded encounter and lets the user dial
// each npc's damage credit between 0% and 100%; damage-done meters
// then count damage to that mob at the chosen fraction.
class UIMobWeightPanel {
public:
    UIMobWeightPanel();
    ~UIMobWeightPanel();

    // Render the panel (no-op while hidden)
    void render(const CombatDatabase* combatDb,
                const std::unordered_map<std::string, std::string>* guidToName);

    // Visibility control
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    void toggleVisible() { visible_ = !visible_; }

    // True once after any edit since the last call; the owner polls
    // this to refresh the combat database's weight cache
    bool consumeWeightsChanged() {
        bool changed = weightsChanged_;
        weightsChanged_ = false;
        return changed;
    }

    // Size ImGui laid out on the last rendered frame; (0,0) until the
    // panel has rendered once. The overlay's auto-grow uses this so
    // the panel isn't clipped by the small at-a-glance window.
    ImVec2 getLastMeasuredSize() const { return lastMeasuredSize_; }

private:
    // One row per npc id, aggregated across every spawn of that mob
    struct CreatureRow {
        uint32_t npcId = 0;
        std::string name;
        size_t hitCount = 0;  // damage-taken records summed over spawns
    };

    bool visible_ = false;
    bool weightsChanged_ = false;
    ImVec2 lastMeasuredSize_ = ImVec2(0, 0);

    // Row list cached per combat database. Rebuilt when the panel opens,
    // when the database pointer changes, or when the loaded segment's
    // data changes underneath a stable pointer (the overlay reuses one
    // CombatDatabase and rebuilds it in place on every segment switch, so
    // the pointer alone can't tell us the contents moved). builtFingerprint_
    // captures max-timestamp + row count so a segment change auto-refreshes
    // without a manual click.
    const CombatDatabase* cachedDb_ = nullptr;
    uint64_t builtFingerprint_ = 0;
    bool wasVisible_ = false;
    std::vector<CreatureRow> rows_;

    // Cheap "has the loaded data changed?" fingerprint for combatDb.
    static uint64_t databaseFingerprint(const CombatDatabase* combatDb);

    void rebuildRows(const CombatDatabase* combatDb,
                     const std::unordered_map<std::string, std::string>* guidToName);

    // Persist current weights to the settings cache and raise the
    // changed flag for the owner
    void saveAndFlagChanged();

    void renderCreatureRow(const CreatureRow& row);
    void renderStaleEntries();
};
