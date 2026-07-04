#pragma once
#include "Parser.h"
#include <charconv>
#include <vector>

namespace parser {


    struct ChallengeModeStartTag {};

    struct ChallengeModeEndTag {};

    // Parser specialization for CHALLENGE_MODE_START
    // Format: Date, Time, CHALLENGE_MODE_START, "zoneName", instanceID, challengeModeID, keystoneLevel, [affixID, ...]
    // Example: 12/9 21:48:35.123  CHALLENGE_MODE_START,"Mists of Tirna Scithe",2290,375,11,[9,122,4,121]
    template<>
    struct EventParser<ChallengeModeStartTag> {
        static constexpr size_t expected_token_count() {
            return 8;  // Date, Time, EventType, zoneName, instanceID, challengeModeID, keystoneLevel, affixes
        }

        struct ChallengeModeStartData {
            uint64_t timestamp_ms;
            std::string_view zoneName;
            uint32_t instanceId;
            uint32_t challengeModeId;
            uint32_t keystoneLevel;
            std::vector<uint32_t> affixIds;  // Variable length array of affix IDs
        };

        static ChallengeModeStartData parse_and_return(const std::vector<std::string_view>& tokens) {
            ChallengeModeStartData data{};

            // Parse timestamp
            data.timestamp_ms = EventParser<void>::parse_timestamp_ms(tokens);

            // Initialize log start timestamp if not set
            auto& log_start_ts = get_log_start_timestamp_ms();
            if (log_start_ts == 0) {
                log_start_ts = data.timestamp_ms;
            }

            // Token[0]: Date
            // Token[1]: Time
            // Token[2]: "CHALLENGE_MODE_START"
            // Token[3]: zoneName (quoted string)
            // Token[4]: instanceID
            // Token[5]: challengeModeID
            // Token[6]: keystoneLevel
            // Token[7]: affixes array like "[9,122,4,121]"

            data.zoneName = tokens[3];
            std::from_chars(tokens[4].data(), tokens[4].data() + tokens[4].size(), data.instanceId);
            std::from_chars(tokens[5].data(), tokens[5].data() + tokens[5].size(), data.challengeModeId);
            std::from_chars(tokens[6].data(), tokens[6].data() + tokens[6].size(), data.keystoneLevel);

            // Parse affixes array if present
            if (tokens.size() > 7) {
                std::string_view affixStr = tokens[7];
                // Strip brackets [...]
                if (affixStr.size() >= 2 && affixStr.front() == '[' && affixStr.back() == ']') {
                    affixStr = affixStr.substr(1, affixStr.size() - 2);
                }

                // Parse comma-separated affix IDs
                size_t pos = 0;
                while (pos < affixStr.size()) {
                    // Find next comma or end
                    size_t end = affixStr.find(',', pos);
                    if (end == std::string_view::npos) {
                        end = affixStr.size();
                    }

                    uint32_t affixId = 0;
                    std::from_chars(affixStr.data() + pos, affixStr.data() + end, affixId);
                    if (affixId != 0) {
                        data.affixIds.push_back(affixId);
                    }
                    pos = end + 1;
                }
            }

            return data;
        }
    };

    // Parser specialization for CHALLENGE_MODE_END
    // Format: Date, Time, CHALLENGE_MODE_END, instanceID, success, keystoneLevel, totalTime
    // Example: 12/9 22:15:42.456  CHALLENGE_MODE_END,2290,1,11,1614523
    template<>
    struct EventParser<ChallengeModeEndTag> {
        static constexpr size_t expected_token_count() {
            return 7;  // Date, Time, EventType, instanceID, success, keystoneLevel, totalTime
        }

        struct ChallengeModeEndData {
            uint64_t timestamp_ms;
            uint32_t instanceId;
            uint32_t success;        // 0 = failed, 1 = completed in time
            uint32_t keystoneLevel;
            uint32_t totalTime_ms;   // Total run time in milliseconds (includes death penalties)
        };

        static ChallengeModeEndData parse_and_return(const std::vector<std::string_view>& tokens) {
            ChallengeModeEndData data{};

            // Parse timestamp
            data.timestamp_ms = EventParser<void>::parse_timestamp_ms(tokens);

            // Token[0]: Date
            // Token[1]: Time
            // Token[2]: "CHALLENGE_MODE_END"
            // Token[3]: instanceID
            // Token[4]: success (0 or 1)
            // Token[5]: keystoneLevel
            // Token[6]: totalTime (ms)

            std::from_chars(tokens[3].data(), tokens[3].data() + tokens[3].size(), data.instanceId);
            std::from_chars(tokens[4].data(), tokens[4].data() + tokens[4].size(), data.success);
            std::from_chars(tokens[5].data(), tokens[5].data() + tokens[5].size(), data.keystoneLevel);
            std::from_chars(tokens[6].data(), tokens[6].data() + tokens[6].size(), data.totalTime_ms);

            return data;
        }
    };

} // namespace parser
