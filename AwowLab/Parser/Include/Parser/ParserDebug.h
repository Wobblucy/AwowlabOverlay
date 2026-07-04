#pragma once
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <set>
#include <vector>
#include <algorithm>

namespace parser {

// =============================================================================
// Debug Logging Macros
// Disabled by default - only failure logging is active
// =============================================================================

#define PARSER_DEBUG(eventName, msg) ((void)0)
#define PARSER_DEBUG_TOKENS(eventName, tokens, msg) ((void)0)
#define PARSER_DEBUG_VERBOSE(eventName, ...) ((void)0)

// =============================================================================
// Debug Statistics Collection
// =============================================================================

#ifndef NDEBUG
namespace debug {

    // Track observed token counts per event type
    inline std::unordered_map<std::string, std::set<size_t>> observedTokenCounts;

    inline void recordTokenCount(std::string_view eventType, size_t count) {
        observedTokenCounts[std::string(eventType)].insert(count);
    }

    inline void printTokenCountReport() {
        std::cout << "\n=== Token Count Report ===\n";
        for (const auto& [event, counts] : observedTokenCounts) {
            std::cout << event << ": ";
            for (size_t c : counts) std::cout << c << " ";
            std::cout << "\n";
        }
    }

    // =============================================================================
    // Parse Failure Tracking (captures samples per actor type)
    // =============================================================================

    struct ParseFailureSample {
        std::string eventType;
        std::string actorType;   // Player, Creature, Vehicle, Pet, Unknown
        size_t tokenCount;
        std::string rawTokens;   // First ~15 tokens reconstructed
    };

    // Map: eventType -> (actorType -> sample)
    // This ensures we capture up to one sample per actor type per event type
    inline std::unordered_map<std::string,
        std::unordered_map<std::string, ParseFailureSample>> parseFailures;

    inline std::string getActorTypeFromGUID(std::string_view guid) {
        if (guid.size() >= 7 && guid.substr(0, 7) == "Player-") return "Player";
        if (guid.size() >= 9 && guid.substr(0, 9) == "Creature-") return "Creature";
        if (guid.size() >= 8 && guid.substr(0, 8) == "Vehicle-") return "Vehicle";
        if (guid.size() >= 4 && guid.substr(0, 4) == "Pet-") return "Pet";
        if (guid == "0000000000000000" || guid == "nil") return "Nil";
        return "Unknown";
    }

    inline void recordParseFailure(
        std::string_view eventType,
        std::string_view sourceGUID,
        size_t tokenCount,
        const std::vector<std::string_view>& tokens
    ) {
        std::string eventStr(eventType);
        std::string actorType = getActorTypeFromGUID(sourceGUID);

        auto& eventFailures = parseFailures[eventStr];

        // Only record if we don't have a sample for this actor type yet
        if (eventFailures.find(actorType) == eventFailures.end()) {
            ParseFailureSample sample;
            sample.eventType = eventStr;
            sample.actorType = actorType;
            sample.tokenCount = tokenCount;

            // Reconstruct first ~15 tokens for debugging
            std::string rawTokens;
            size_t limit = std::min(tokens.size(), size_t(15));
            for (size_t i = 0; i < limit; ++i) {
                if (i > 0) rawTokens += ",";
                rawTokens += tokens[i];
            }
            if (tokens.size() > 15) {
                rawTokens += "...";
            }
            sample.rawTokens = rawTokens.substr(0, 300);  // Truncate if very long

            eventFailures[actorType] = std::move(sample);

            // Also print immediately for debugging
            std::cout << "[PARSE_FAIL] " << eventStr
                      << " [" << actorType << "] tokens=" << tokenCount << "\n"
                      << "  " << sample.rawTokens << "\n";
        }
    }

    inline void printParseFailureReport() {
        if (parseFailures.empty()) {
            std::cout << "\n=== No Parse Failures Recorded ===\n";
            return;
        }

        std::cout << "\n=== Parse Failure Report ===\n";
        for (const auto& [event, actorSamples] : parseFailures) {
            std::cout << event << ":\n";
            for (const auto& [actorType, sample] : actorSamples) {
                std::cout << "  [" << actorType << "] tokens=" << sample.tokenCount
                          << "\n    " << sample.rawTokens << "\n";
            }
        }
    }

    // =============================================================================
    // Unhandled Event Tracking
    // =============================================================================

    inline std::unordered_map<std::string, size_t> unhandledEventCounts;

    inline void recordUnhandledEvent(std::string_view eventType) {
        unhandledEventCounts[std::string(eventType)]++;
    }

    inline void printUnhandledEventReport() {
        if (unhandledEventCounts.empty()) {
            std::cout << "\n=== All Events Handled ===\n";
            return;
        }

        std::cout << "\n=== Unhandled Event Report ===\n";
        for (const auto& [event, count] : unhandledEventCounts) {
            std::cout << "  " << event << ": " << count << " occurrences\n";
        }
    }

    // Print all debug reports
    inline void printAllReports() {
        printTokenCountReport();
        printParseFailureReport();
        printUnhandledEventReport();
    }

    // Clear all statistics (call when loading a new log file)
    inline void clearAllStatistics() {
        observedTokenCounts.clear();
        parseFailures.clear();
        unhandledEventCounts.clear();
    }

} // namespace debug
#endif // NDEBUG

} // namespace parser
