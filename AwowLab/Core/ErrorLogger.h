#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

// Singleton error logger that writes to file and collects warnings for UI display
class ErrorLogger {
public:
    struct Warning {
        std::string message;
        std::string timestamp;
        std::string category;
    };

    static ErrorLogger& instance() {
        static ErrorLogger logger;
        return logger;
    }

    // Initialize with path to executable (log file goes next to exe)
    void initialize(const std::filesystem::path& exePath) {
        std::lock_guard<std::mutex> lock(mutex_);
        logFilePath_ = exePath.parent_path() / "awowlab_errors.log";

        // Open log file in append mode
        logFile_.open(logFilePath_, std::ios::app);
        if (logFile_.is_open()) {
            logFile_ << "\n========== AwowLab Session Started: " << getCurrentTimestamp() << " ==========\n";
            logFile_.flush();
        }
    }

    // Log info (goes to file only, NOT stored for UI - use for debugging/diagnostics)
    void logInfo(const std::string& category, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string timestamp = getCurrentTimestamp();
        std::string fullMessage = "[" + timestamp + "] [INFO:" + category + "] " + message;

        // Write to file only - don't add to UI warnings
        if (logFile_.is_open()) {
            logFile_ << fullMessage << "\n";
            logFile_.flush();
        }
    }

    // Log a warning (goes to file + stored for UI)
    void logWarning(const std::string& category, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string timestamp = getCurrentTimestamp();
        std::string fullMessage = "[" + timestamp + "] [" + category + "] " + message;

        // Write to file
        if (logFile_.is_open()) {
            logFile_ << fullMessage << "\n";
            logFile_.flush();
        }

        // Store for UI
        warnings_.push_back({message, timestamp, category});
        hasNewWarnings_ = true;
    }

    // Log an error (goes to file + stored for UI + triggers popup)
    void logError(const std::string& category, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string timestamp = getCurrentTimestamp();
        std::string fullMessage = "[" + timestamp + "] [ERROR:" + category + "] " + message;

        if (logFile_.is_open()) {
            logFile_ << fullMessage << "\n";
            logFile_.flush();
        }

        warnings_.push_back({message, timestamp, "ERROR:" + category});
        hasNewErrors_ = true;  // Only errors trigger popup
    }

    // Log a fatal crash (goes to file only, immediate flush, no mutex lock to avoid deadlock)
    // Use this only in crash handlers where the app is about to terminate
    void logCrashUnsafe(const std::string& message) {
        std::string timestamp = getCurrentTimestamp();
        std::string fullMessage = "[" + timestamp + "] [CRASH] " + message;

        if (logFile_.is_open()) {
            logFile_ << "\n!!! CRASH DETECTED !!!\n";
            logFile_ << fullMessage << "\n";
            logFile_ << "!!! APPLICATION TERMINATING !!!\n";
            logFile_.flush();
        }
    }

    // Check if there are new errors since last check (for popup trigger)
    bool hasNewErrors() const {
        return hasNewErrors_;
    }

    // Legacy - check if there are any warnings (errors count as warnings)
    bool hasNewWarnings() const {
        return hasNewErrors_;  // Only trigger for errors now
    }

    // Get all warnings and clear the new flag
    std::vector<Warning> getWarnings() {
        std::lock_guard<std::mutex> lock(mutex_);
        hasNewWarnings_ = false;
        hasNewErrors_ = false;
        return warnings_;
    }

    // Clear all warnings
    void clearWarnings() {
        std::lock_guard<std::mutex> lock(mutex_);
        warnings_.clear();
        hasNewWarnings_ = false;
        hasNewErrors_ = false;
    }

    // Get warning count
    size_t warningCount() const {
        return warnings_.size();
    }

    // Get log file path for user reference
    std::filesystem::path getLogFilePath() const {
        return logFilePath_;
    }

private:
    ErrorLogger() = default;
    ~ErrorLogger() {
        if (logFile_.is_open()) {
            logFile_ << "========== Session Ended ==========\n";
            logFile_.close();
        }
    }

    ErrorLogger(const ErrorLogger&) = delete;
    ErrorLogger& operator=(const ErrorLogger&) = delete;

    std::string getCurrentTimestamp() const {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    std::mutex mutex_;
    std::ofstream logFile_;
    std::filesystem::path logFilePath_;
    std::vector<Warning> warnings_;
    bool hasNewWarnings_ = false;
    bool hasNewErrors_ = false;
};

// Convenience macros
#define AWOW_LOG_INFO(category, message) \
    ErrorLogger::instance().logInfo(category, message)

#define AWOW_LOG_WARNING(category, message) \
    ErrorLogger::instance().logWarning(category, message)

#define AWOW_LOG_ERROR(category, message) \
    ErrorLogger::instance().logError(category, message)
