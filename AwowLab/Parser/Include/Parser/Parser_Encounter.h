#pragma once
#include "Parser.h"
#include "Parser_Damage.h"  // For parser::util functions
#include <charconv>

namespace parser {

    // Tag for ENCOUNTER_START events
    struct EncounterStartTag {};

    // Tag for ENCOUNTER_END events
    struct EncounterEndTag {};

    // Parser specialization for ENCOUNTER_START
    // Format: Date, Time, ENCOUNTER_START, encounterID, "encounterName", difficultyID, groupSize
    template<>
    struct EventParser<EncounterStartTag> {
        static constexpr size_t expected_token_count() {
            return 7;  // Date, Time, EventType, encounterID, encounterName, difficultyID, groupSize
        }

        struct EncounterStartData {
            uint64_t timestamp_ms;
            uint32_t encounterId;
            std::string_view encounterName;
            uint32_t difficultyId;
            uint32_t groupSize;
        };

        static EncounterStartData parse_and_return(const std::vector<std::string_view>& tokens) {
            EncounterStartData data{};

            // Parse timestamp
            data.timestamp_ms = EventParser<void>::parse_timestamp_ms(tokens);

            // Initialize log start timestamp if not set
            auto& log_start_ts = get_log_start_timestamp_ms();
            if (log_start_ts == 0) {
                log_start_ts = data.timestamp_ms;
            }

            // Parse encounter details
            // Token[0]: Date
            // Token[1]: Time
            // Token[2]: "ENCOUNTER_START"
            // Token[3]: encounterID
            // Token[4]: encounterName
            // Token[5]: difficultyID
            // Token[6]: groupSize

            data.encounterId = util::parseUint32(tokens[3]);
            data.encounterName = tokens[4];
            data.difficultyId = util::parseUint32(tokens[5]);
            data.groupSize = util::parseUint32(tokens[6]);

            return data;
        }
    };

    // Parser specialization for ENCOUNTER_END
    // Format: Date, Time, ENCOUNTER_END, encounterID, "encounterName", difficultyID, groupSize, success
    template<>
    struct EventParser<EncounterEndTag> {
        static constexpr size_t expected_token_count() {
            return 8;  // Date, Time, EventType, encounterID, encounterName, difficultyID, groupSize, success
        }

        struct EncounterEndData {
            uint64_t timestamp_ms;
            uint32_t encounterId;
            std::string_view encounterName;
            uint32_t difficultyId;
            uint32_t groupSize;
            uint32_t success;  // 0 = wipe, 1 = kill
        };

        static EncounterEndData parse_and_return(const std::vector<std::string_view>& tokens) {
            EncounterEndData data{};

            // Parse timestamp
            data.timestamp_ms = EventParser<void>::parse_timestamp_ms(tokens);

            // Parse encounter details
            // Token[0]: Date
            // Token[1]: Time
            // Token[2]: "ENCOUNTER_END"
            // Token[3]: encounterID
            // Token[4]: encounterName
            // Token[5]: difficultyID
            // Token[6]: groupSize
            // Token[7]: success (0 or 1)

            data.encounterId = util::parseUint32(tokens[3]);
            data.encounterName = tokens[4];
            data.difficultyId = util::parseUint32(tokens[5]);
            data.groupSize = util::parseUint32(tokens[6]);
            data.success = util::parseUint32(tokens[7]);

            return data;
        }
    };

} // namespace parser
