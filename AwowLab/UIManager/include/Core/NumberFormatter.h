#pragma once

#include <string>
#include <cstdint>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace ui::format {

// Format large numbers with B/M/K suffixes (e.g., 1.5M, 250K)
inline std::string formatAmount(int64_t amount) {
    if (amount >= 1000000000) {
        return std::to_string(amount / 1000000000) + "." +
               std::to_string((amount % 1000000000) / 100000000) + "B";
    } else if (amount >= 1000000) {
        return std::to_string(amount / 1000000) + "." +
               std::to_string((amount % 1000000) / 100000) + "M";
    } else if (amount >= 1000) {
        return std::to_string(amount / 1000) + "." +
               std::to_string((amount % 1000) / 100) + "K";
    }
    return std::to_string(amount);
}

// Format rate values (DPS/HPS/DTPS) with M/K suffixes
inline std::string formatPerSecond(float value) {
    if (value >= 1000000.0f) {
        return std::to_string(static_cast<int>(value / 1000000.0f)) + "." +
               std::to_string(static_cast<int>(std::fmod(value, 1000000.0f) / 100000.0f)) + "M";
    } else if (value >= 1000.0f) {
        return std::to_string(static_cast<int>(value / 1000.0f)) + "." +
               std::to_string(static_cast<int>(std::fmod(value, 1000.0f) / 100.0f)) + "K";
    }
    return std::to_string(static_cast<int>(value));
}

// Format timestamp in MM:SS.mmm format (supports negative timestamps for pre-encounter events)
inline std::string formatTimestamp(int32_t timestamp_ms) {
    bool negative = timestamp_ms < 0;
    uint32_t absMs = negative ? static_cast<uint32_t>(-timestamp_ms) : static_cast<uint32_t>(timestamp_ms);

    uint32_t totalSeconds = absMs / 1000;
    uint32_t minutes = totalSeconds / 60;
    uint32_t seconds = totalSeconds % 60;
    uint32_t millis = absMs % 1000;

    std::ostringstream oss;
    if (negative) {
        oss << "-";
    }
    oss << std::setfill('0') << std::setw(2) << minutes << ":"
        << std::setfill('0') << std::setw(2) << seconds << "."
        << std::setfill('0') << std::setw(3) << millis;
    return oss.str();
}

} // namespace ui::format
