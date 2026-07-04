#pragma once
#include <string_view>
#include <vector>
#include <iostream>
#include <charconv>
#include <ctime>
#include <algorithm>
#include "../structures.h"

namespace parser {

    // Shared log start timestamp across all event parsers
    // This ensures all events use the same time baseline
    inline uint64_t& get_log_start_timestamp_ms() {
        static uint64_t ts = 0;
        return ts;
    }

    template <typename EventType>
    struct EventParser {

        static constexpr size_t expected_token_count() {
            return 0;
        }

        static bool check_size(const std::vector<std::string_view>& tokens) {
            #ifdef NDEBUG
                        return true;
            #endif // NDEBUG


            if (tokens.size() != expected_token_count()) {
                std::cerr << "Error [Base]: expected " << expected_token_count()
                    << " tokens, got " << tokens.size();
                if (tokens.size() >= 3) {
                    std::cerr << " (event: " << tokens[2] << ")";
                }
                std::cerr << "\n";
                return false;
            }
            return true;
        }

        // Returns 0 for lines whose date/time fields are malformed.
        static uint64_t parse_timestamp_ms(const std::vector<std::string_view>& tokens) {

            const std::string_view date = tokens[0];
            const std::string_view time = tokens[1];

            // Date format is MM/DD/YYYY
            auto first_slash = date.find('/');
            if (first_slash == std::string_view::npos) return 0;
            auto second_slash = date.find('/', first_slash + 1);
            if (second_slash == std::string_view::npos) return 0;

            // Time format is HH:MM:SS.mmm (may carry a UTC-offset suffix we ignore)
            auto h_end = time.find(':');
            if (h_end == std::string_view::npos) return 0;
            auto m2_end = time.find(':', h_end + 1);
            if (m2_end == std::string_view::npos) return 0;
            auto s_end = time.find('.', m2_end + 1);
            if (s_end == std::string_view::npos) return 0;

            int year = 0, month = 0, day = 0;
            int hour = 0, minute = 0, second = 0, milliseconds = 0;

            auto parsed = [](const char* first, const char* last, int& out) {
                return std::from_chars(first, last, out).ec == std::errc();
            };

            bool ok = parsed(date.data(), date.data() + first_slash, month)
                   && parsed(date.data() + first_slash + 1, date.data() + second_slash, day)
                   && parsed(date.data() + second_slash + 1, date.data() + date.size(), year)
                   && parsed(time.data(), time.data() + h_end, hour)
                   && parsed(time.data() + h_end + 1, time.data() + m2_end, minute)
                   && parsed(time.data() + m2_end + 1, time.data() + s_end, second);
            if (!ok) return 0;

            size_t ms_start = s_end + 1;
            size_t ms_end = (std::min)(ms_start + 3, time.size());  // Truncate to 3 chars max (parentheses prevent macro expansion)
            std::from_chars(time.data() + ms_start, time.data() + ms_end, milliseconds);

            // mktime costs ~200 cycles and this runs for every event in the
            // log, but the date changes at most once per log (midnight
            // rollover). Convert the date once and reuse its midnight epoch.
            // The tm struct is zero-initialized (tm_isdst = 0, standard
            // time), which makes mktime a plain calendar conversion - so
            // midnight + h*3600 matches the old per-event call exactly.
            thread_local int cached_year = 0, cached_month = 0, cached_day = 0;
            thread_local std::time_t cached_midnight = 0;
            if (year != cached_year || month != cached_month || day != cached_day) {
                std::tm tm{};
                tm.tm_year = year - 1900;
                tm.tm_mon = month - 1;
                tm.tm_mday = day;
                cached_midnight = std::mktime(&tm);
                cached_year = year;
                cached_month = month;
                cached_day = day;
            }

            std::time_t sec_epoch = cached_midnight + hour * 3600 + minute * 60 + second;
            return static_cast<uint64_t>(sec_epoch) * 1000ull + static_cast<uint64_t>(milliseconds);
        };

        static void parse(const std::vector<std::string_view>& tokens) {
            std::cerr << "Parsing not implemented for "
                << (tokens.size() > 2 ? std::string(tokens[2]) : "unknown event") << "\n";
        }
    };

    inline void ParseEvent([[maybe_unused]] const std::vector<std::string_view>& tokens, [[maybe_unused]] std::vector<UnitRendering>& urTable) {
    }

} // namespace parser
