#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Boss phase tracking. Retail combat logs carry no phase markers, so
// phases are derived from the fight itself. A rule marks where a new
// phase starts, and re-applies automatically to every pull of that
// encounter. Three rule kinds:
//   SpellCast - first cast of a boss ability starts the phase
//   Elapsed   - the phase starts a fixed time into the pull
//   Emote     - the first matching boss emote starts the phase
// An encounter can hold any number of rules; boundaries are sorted by
// their resolved time at load, so rule order never matters.
//
// Rules are keyed by encounter id (straight from ENCOUNTER_START).
// Persistence stays with the caller (same arrangement as
// MobWeightSettings): toJson/fromJson round-trip through the settings
// cache, and this class keeps zero other dependencies.
class PhaseSettings {
public:
    enum class RuleKind : uint8_t {
        SpellCast,  // spellId set
        Elapsed,    // elapsedMs set (from pull start)
        Emote,      // emoteText set (cleaned emote text, exact match)
    };

    struct PhaseRule {
        RuleKind kind = RuleKind::SpellCast;
        uint32_t spellId = 0;
        int32_t elapsedMs = 0;
        std::string emoteText;
        std::string label;  // Optional display label; "P2"-style when empty

        bool matches(const PhaseRule& other) const {
            if (kind != other.kind) return false;
            switch (kind) {
                case RuleKind::SpellCast: return spellId == other.spellId;
                case RuleKind::Elapsed:   return elapsedMs == other.elapsedMs;
                case RuleKind::Emote:     return emoteText == other.emoteText;
            }
            return false;
        }
    };

    static PhaseSettings& instance();

    // encounter id -> phase split rules. Rule order doesn't matter;
    // phases are numbered by resolved boundary time at load.
    std::unordered_map<uint32_t, std::vector<PhaseRule>> rules;

    // Rules for an encounter, nullptr when none are set
    const std::vector<PhaseRule>* rulesFor(uint32_t encounterId) const;

    // Add a rule (duplicate of an existing rule is a no-op)
    void addRule(uint32_t encounterId, const PhaseRule& rule);

    // Convenience for spell-cast rules (the context-menu path)
    void addRule(uint32_t encounterId, uint32_t spellId, const std::string& label = {});
    void removeRule(uint32_t encounterId, uint32_t spellId);
    bool hasRule(uint32_t encounterId, uint32_t spellId) const;

    // Remove by full rule identity (works for every kind)
    void removeRule(uint32_t encounterId, const PhaseRule& rule);

    std::string toJson() const;
    void fromJson(const std::string& json);

private:
    PhaseSettings() = default;
};
