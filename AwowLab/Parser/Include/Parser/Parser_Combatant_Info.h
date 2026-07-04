#pragma once
#include <vector>
#include <string_view>
#include <iostream>
#include <charconv>
#include "../structures.h"
#include "Parser.h"
#include "WowClass.h"

namespace parser {

    struct CombatantInfoTag {};

    // Struct to hold talent data from COMBATANT_INFO
    struct TalentEntry {
        uint32_t talent_id = 0;    // Talent node ID (from talent.db2)
        uint32_t spell_id = 0;     // Spell granted by this talent
        uint8_t rank = 0;          // Talent rank (1-2 for multi-rank talents)
    };

    // Struct to hold extracted combatant info
    struct CombatantInfoData {
        std::string_view player_guid;
        uint16_t spec_id = 0;
        int32_t haste_melee = 0;    // Haste rating (not percentage)
        int32_t haste_ranged = 0;   // Haste rating (not percentage)
        int32_t haste_spell = 0;    // Haste rating (not percentage)
        std::vector<TalentEntry> talents;  // Talents from token 29+
    };

    template <>
    struct EventParser<CombatantInfoTag> {
        // COMBATANT_INFO has variable token count due to arrays at the end
        // Token layout (0-indexed):
        // [0] = timestamp date
        // [1] = timestamp time
        // [2] = event type (COMBATANT_INFO)
        // [3] = playerGUID
        // [4] = faction
        // [5-26/27] = 22-23 character stats
        //          WoW 11.x: 22 stats (Strength through Armor)
        //          WoW 12.0: 23 stats (added UnknownField12_0 after Armor)
        //          Strength, Agility, Stamina, Intelligence, Dodge, Parry, Block,
        //          CritMelee, CritRanged, CritSpell, Speed, Lifesteal, HasteMelee,
        //          HasteRanged, HasteSpell, Avoidance, Mastery, VersatilityDamageDone,
        //          VersatilityHealingDone, VersatilityDamageTaken, Armor, [UnknownField12_0]
        // [27/28] = CurrentSpecID (varies by expansion)
        // [28/29+] = talentTree array
        // Minimum tokens needed:
        //   WoW 11.x: timestamp(2) + event(1) + playerGUID(1) + faction(1) + stats(22) + specID(1) = 28
        //   WoW 12.0: timestamp(2) + event(1) + playerGUID(1) + faction(1) + stats(23) + specID(1) = 29
        static constexpr size_t minimum_token_count() {
            return 28;  // Support both 11.x (28 tokens) and 12.0 (29 tokens)
        }

        static constexpr size_t TOKEN_PLAYER_GUID = 3;
        static constexpr size_t TOKEN_HASTE_MELEE = 17;   // HasteMelee rating
        static constexpr size_t TOKEN_HASTE_RANGED = 18;  // HasteRanged rating
        static constexpr size_t TOKEN_HASTE_SPELL = 19;   // HasteSpell rating
        // Spec ID index varies by expansion:
        // WoW 11.x: index 27 (22 stats)
        // WoW 12.0: index 28 (23 stats)
        static constexpr size_t TOKEN_SPEC_ID_11X = 27;   // WoW 11.x
        static constexpr size_t TOKEN_SPEC_ID_12X = 28;   // WoW 12.0
        static constexpr size_t TOKEN_TALENTS_11X = 28;   // Talent array for 11.x
        static constexpr size_t TOKEN_TALENTS_12X = 29;   // Talent array for 12.0

        // Helper: Parse talent array from token string
        // Format: [(talent_id,spell_id,rank),(talent_id,spell_id,rank),...]
        static std::vector<TalentEntry> parse_talent_array(std::string_view token_str) {
            std::vector<TalentEntry> talents;

            // Find opening bracket
            size_t pos = token_str.find('[');
            if (pos == std::string_view::npos) {
                return talents;  // No talent array
            }

            pos++;  // Skip '['

            while (pos < token_str.size()) {
                // Skip whitespace
                while (pos < token_str.size() && (token_str[pos] == ' ' || token_str[pos] == ',')) {
                    pos++;
                }

                if (pos >= token_str.size() || token_str[pos] == ']') {
                    break;  // End of array
                }

                // Expect opening parenthesis
                if (token_str[pos] != '(') {
                    break;  // Malformed
                }
                pos++;  // Skip '('

                TalentEntry entry;

                // Parse talent_id
                auto start = pos;
                while (pos < token_str.size() && token_str[pos] != ',') pos++;
                if (pos >= token_str.size()) break;
                std::from_chars(token_str.data() + start, token_str.data() + pos, entry.talent_id);
                pos++;  // Skip ','

                // Parse spell_id
                start = pos;
                while (pos < token_str.size() && token_str[pos] != ',') pos++;
                if (pos >= token_str.size()) break;
                std::from_chars(token_str.data() + start, token_str.data() + pos, entry.spell_id);
                pos++;  // Skip ','

                // Parse rank
                start = pos;
                while (pos < token_str.size() && token_str[pos] != ')') pos++;
                if (pos >= token_str.size()) break;
                std::from_chars(token_str.data() + start, token_str.data() + pos, entry.rank);
                pos++;  // Skip ')'

                talents.push_back(entry);
            }

            return talents;
        }

        // Helper: Check if a value is a valid WoW spec ID
        static bool looks_like_spec_id(uint16_t val) {
            // Use authoritative spec list from WowClass.h
            return getClassFromSpecId(val) != WowClass::Unknown;
        }

        // Locate the spec id and talent-array token without touching the
        // arrays themselves. Fuzzy matching: scan tokens 25-30 for a valid
        // spec ID value followed shortly by a '['-token (the talent array);
        // this handles variations between WoW versions (different stat
        // field counts). Falls back to the fixed per-version positions.
        // Returns {specId, specIdIndex, talentIndex}; specIdIndex stays 0
        // when only the fixed-position fallback matched.
        struct SpecIdLocation {
            uint16_t spec_id = 0;
            size_t spec_index = 0;
            size_t talent_index = 0;
        };
        static SpecIdLocation find_spec_id(const std::vector<std::string_view>& tokens) {
            SpecIdLocation loc;

            for (size_t i = 25; i < (std::min)(tokens.size(), size_t(35)); ++i) {
                uint16_t testVal = 0;
                auto result = std::from_chars(tokens[i].data(),
                                              tokens[i].data() + tokens[i].size(),
                                              testVal);

                if (result.ec == std::errc{} && looks_like_spec_id(testVal)) {
                    // Found a valid spec ID! Check if next non-empty token is talent array
                    for (size_t j = i + 1; j < (std::min)(tokens.size(), i + 5); ++j) {
                        if (!tokens[j].empty() && tokens[j][0] == '[') {
                            // Confirmed: this is spec ID, next is talent array
                            loc.spec_index = i;
                            loc.talent_index = j;
                            loc.spec_id = testVal;
                            break;
                        }
                    }
                    if (loc.spec_index > 0) break;
                }
            }

            // If fuzzy matching failed, fall back to fixed positions
            if (loc.spec_index == 0) {
                // Try 12.0 position first, then 11.x
                bool is12x = (tokens.size() >= 29);
                size_t specIdIndex = is12x ? TOKEN_SPEC_ID_12X : TOKEN_SPEC_ID_11X;
                loc.talent_index = is12x ? TOKEN_TALENTS_12X : TOKEN_TALENTS_11X;

                std::from_chars(tokens[specIdIndex].data(),
                                tokens[specIdIndex].data() + tokens[specIdIndex].size(),
                                loc.spec_id);
            }

            return loc;
        }

        // Parse all relevant combatant info
        static CombatantInfoData parse_and_return(const std::vector<std::string_view>& tokens) {
            CombatantInfoData data;

            if (tokens.size() < minimum_token_count()) {
                std::cerr << "Error [COMBATANT_INFO]: expected at least " << minimum_token_count()
                    << " tokens, got " << tokens.size() << "\n";
                return data;
            }

            data.player_guid = tokens[TOKEN_PLAYER_GUID];

            // Spec id + talent array position (fuzzy scan with fixed
            // fallback; see find_spec_id)
            SpecIdLocation loc = find_spec_id(tokens);
            data.spec_id = loc.spec_id;
            size_t talentIndex = loc.talent_index;

            // Parse haste values (these positions are stable across versions)
            std::from_chars(tokens[TOKEN_HASTE_MELEE].data(),
                           tokens[TOKEN_HASTE_MELEE].data() + tokens[TOKEN_HASTE_MELEE].size(),
                           data.haste_melee);

            std::from_chars(tokens[TOKEN_HASTE_RANGED].data(),
                           tokens[TOKEN_HASTE_RANGED].data() + tokens[TOKEN_HASTE_RANGED].size(),
                           data.haste_ranged);

            std::from_chars(tokens[TOKEN_HASTE_SPELL].data(),
                           tokens[TOKEN_HASTE_SPELL].data() + tokens[TOKEN_HASTE_SPELL].size(),
                           data.haste_spell);

            // Parse talents array if we found it
            if (talentIndex > 0 && talentIndex < tokens.size() &&
                !tokens[talentIndex].empty() && tokens[talentIndex][0] == '[') {
                // Find all tokens that are part of the talent array and concatenate them
                std::string talent_str;
                talent_str.reserve(2048);  // Pre-allocate for typical talent list
                for (size_t i = talentIndex; i < tokens.size(); ++i) {
                    if (i > talentIndex) {
                        talent_str += ',';  // Re-add comma separator
                    }
                    talent_str.append(tokens[i].data(), tokens[i].size());

                    // Stop when we find the closing bracket of the talent array
                    if (!tokens[i].empty() && tokens[i].back() == ']') {
                        break;
                    }
                }
                data.talents = parse_talent_array(talent_str);
            }

#ifdef DEBUG
            std::cout << "[CombatantInfo] Parsed:\n"
                << "  playerGUID   = " << data.player_guid << "\n"
                << "  specID       = " << data.spec_id << "\n"
                << "  hasteMelee   = " << data.haste_melee << "\n"
                << "  hasteRanged  = " << data.haste_ranged << "\n"
                << "  hasteSpell   = " << data.haste_spell << "\n"
                << "  talents      = " << data.talents.size() << " entries\n";
#endif

            return data;
        }

        // Spec id only - skips the haste fields and the talent/equipment
        // arrays entirely. The live overlay path uses this per player at
        // encounter start.
        static uint16_t extract_spec_id(const std::vector<std::string_view>& tokens) {
            if (tokens.size() < minimum_token_count()) {
                return 0;
            }
            return find_spec_id(tokens).spec_id;
        }

        static std::string_view extract_player_guid(const std::vector<std::string_view>& tokens) {
            if (tokens.size() < minimum_token_count()) {
                return {};
            }
            return tokens[TOKEN_PLAYER_GUID];
        }
    };

} // namespace parser
