#pragma once
#include <filesystem>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <optional>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#endif

// Watches a WoW Logs folder for combat log file changes using ReadDirectoryChangesW.
// Runs a background thread that monitors for file writes to WoWCombatLog*.txt files.
// When WoW writes to the combat log, this class signals that new data is available.
//
// Usage:
//   ActiveLogWatcher watcher;
//   watcher.start("C:/Program Files/World of Warcraft/_retail_/Logs");
//
//   // In your main loop:
//   if (watcher.hasNewWrite()) {
//       auto logPath = watcher.getActiveLogPath();
//       // Parse new data from logPath
//   }
//
class ActiveLogWatcher {
public:
    using WriteCallback = std::function<void(const std::filesystem::path&)>;

    ActiveLogWatcher();
    ~ActiveLogWatcher();

    // Non-copyable
    ActiveLogWatcher(const ActiveLogWatcher&) = delete;
    ActiveLogWatcher& operator=(const ActiveLogWatcher&) = delete;

    // Start watching a WoW Logs folder for combat log changes
    // Returns true if watching started successfully
    bool start(const std::filesystem::path& logsFolder);

    // Stop watching (automatically called by destructor)
    void stop();

    // Check if the watcher is currently running
    bool isRunning() const { return running_.load(); }

    // Get the folder being watched
    const std::filesystem::path& getLogsFolder() const { return logsFolder_; }

    // Get the currently active log file (most recently modified WoWCombatLog*.txt)
    // Returns nullopt if no combat log file has been detected yet
    std::optional<std::filesystem::path> getActiveLogPath() const;

    // Check if a write has occurred since the last call to hasNewWrite()
    // This resets the flag, so calling twice in a row returns false the second time
    bool hasNewWrite();

    // Get the timestamp of the last detected write
    std::chrono::steady_clock::time_point getLastWriteTime() const;

    // Get seconds since last write (for staleness indicator)
    std::chrono::seconds getSecondsSinceLastWrite() const;

    // Set a callback to be invoked when a new write is detected
    // The callback receives the path to the modified log file
    // Note: Callback is invoked from the watcher thread, so it must be thread-safe
    void setWriteCallback(WriteCallback callback);

private:
    std::filesystem::path logsFolder_;
    std::filesystem::path activeLogPath_;
    std::chrono::steady_clock::time_point lastWriteTime_;

    std::thread watcherThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> newWriteFlag_{false};

    mutable std::mutex mutex_;
    WriteCallback writeCallback_;

#ifdef _WIN32
    HANDLE directoryHandle_ = INVALID_HANDLE_VALUE;
    HANDLE stopEvent_ = NULL;
#endif

    // Background thread function
    void watcherLoop();

    // Find the most recently modified WoWCombatLog*.txt file in the logs folder
    std::optional<std::filesystem::path> findActiveLogFile() const;

    // Check if a filename matches the WoW combat log pattern
    static bool isCombatLogFile(const std::filesystem::path& filename);
};
