#include "ActiveLogWatcher.h"
#include <algorithm>
#include <iostream>

ActiveLogWatcher::ActiveLogWatcher() = default;

ActiveLogWatcher::~ActiveLogWatcher() {
    stop();
}

bool ActiveLogWatcher::start(const std::filesystem::path& logsFolder) {
    // Don't start if already running
    if (running_.load()) {
        return false;
    }

    // Verify the folder exists
    if (!std::filesystem::exists(logsFolder) || !std::filesystem::is_directory(logsFolder)) {
        return false;
    }

    logsFolder_ = logsFolder;
    stopRequested_.store(false);
    newWriteFlag_.store(false);
    lastWriteTime_ = std::chrono::steady_clock::now();

    // Find the initial active log file
    auto initialLog = findActiveLogFile();
    if (initialLog) {
        std::lock_guard<std::mutex> lock(mutex_);
        activeLogPath_ = *initialLog;
    }

#ifdef _WIN32
    // Open the directory for monitoring
    directoryHandle_ = CreateFileW(
        logsFolder_.wstring().c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (directoryHandle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Create an event for signaling stop
    stopEvent_ = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (stopEvent_ == NULL) {
        CloseHandle(directoryHandle_);
        directoryHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }
#endif

    // Start the watcher thread
    running_.store(true);
    watcherThread_ = std::thread(&ActiveLogWatcher::watcherLoop, this);

    return true;
}

void ActiveLogWatcher::stop() {
    if (!running_.load()) {
        return;
    }

    stopRequested_.store(true);

#ifdef _WIN32
    // Signal the stop event to wake up the watcher thread
    if (stopEvent_ != NULL) {
        SetEvent(stopEvent_);
    }
#endif

    // Wait for the thread to finish
    if (watcherThread_.joinable()) {
        watcherThread_.join();
    }

    running_.store(false);

#ifdef _WIN32
    // Clean up handles
    if (directoryHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(directoryHandle_);
        directoryHandle_ = INVALID_HANDLE_VALUE;
    }
    if (stopEvent_ != NULL) {
        CloseHandle(stopEvent_);
        stopEvent_ = NULL;
    }
#endif
}

std::optional<std::filesystem::path> ActiveLogWatcher::getActiveLogPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (activeLogPath_.empty()) {
        return std::nullopt;
    }
    return activeLogPath_;
}

bool ActiveLogWatcher::hasNewWrite() {
    return newWriteFlag_.exchange(false);
}

std::chrono::steady_clock::time_point ActiveLogWatcher::getLastWriteTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastWriteTime_;
}

std::chrono::seconds ActiveLogWatcher::getSecondsSinceLastWrite() const {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    return std::chrono::duration_cast<std::chrono::seconds>(now - lastWriteTime_);
}

void ActiveLogWatcher::setWriteCallback(WriteCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    writeCallback_ = std::move(callback);
}

void ActiveLogWatcher::watcherLoop() {
#ifdef _WIN32
    constexpr DWORD bufferSize = 4096;
    std::vector<BYTE> buffer(bufferSize);
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (overlapped.hEvent == NULL) {
        running_.store(false);
        return;
    }

    while (!stopRequested_.load()) {
        // Start async directory change notification
        DWORD bytesReturned = 0;
        BOOL success = ReadDirectoryChangesW(
            directoryHandle_,
            buffer.data(),
            bufferSize,
            FALSE,  // Don't watch subdirectories
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_FILE_NAME,
            &bytesReturned,
            &overlapped,
            NULL
        );

        if (!success) {
            // If the call failed, wait a bit and try again
            HANDLE waitHandles[] = { stopEvent_ };
            WaitForMultipleObjects(1, waitHandles, FALSE, 1000);
            continue;
        }

        // Wait for either a directory change or a stop signal
        HANDLE waitHandles[] = { overlapped.hEvent, stopEvent_ };
        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

        if (waitResult == WAIT_OBJECT_0 + 1) {
            // Stop event signaled
            CancelIo(directoryHandle_);
            break;
        }

        if (waitResult == WAIT_OBJECT_0) {
            // Directory change notification received
            if (!GetOverlappedResult(directoryHandle_, &overlapped, &bytesReturned, FALSE)) {
                ResetEvent(overlapped.hEvent);
                continue;
            }

            // Process the notifications
            FILE_NOTIFY_INFORMATION* notification = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer.data());

            while (notification) {
                // Extract filename
                std::wstring filename(notification->FileName, notification->FileNameLength / sizeof(WCHAR));
                std::filesystem::path filePath = logsFolder_ / filename;

                // Check if this is a combat log file
                if (isCombatLogFile(filePath.filename())) {
                    // Update the active log path and timestamp
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        activeLogPath_ = filePath;
                        lastWriteTime_ = std::chrono::steady_clock::now();

                        // Invoke callback if set
                        if (writeCallback_) {
                            writeCallback_(filePath);
                        }
                    }

                    newWriteFlag_.store(true);
                }

                // Move to next notification (if any)
                if (notification->NextEntryOffset == 0) {
                    break;
                }
                notification = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<BYTE*>(notification) + notification->NextEntryOffset
                );
            }

            ResetEvent(overlapped.hEvent);
        }
    }

    CloseHandle(overlapped.hEvent);
#else
    // Non-Windows: fall back to polling (not implemented for this Windows-only app)
    while (!stopRequested_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        auto newLog = findActiveLogFile();
        if (newLog) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeLogPath_ != *newLog ||
                std::filesystem::last_write_time(*newLog) > std::filesystem::last_write_time(activeLogPath_)) {
                activeLogPath_ = *newLog;
                lastWriteTime_ = std::chrono::steady_clock::now();
                newWriteFlag_.store(true);
            }
        }
    }
#endif
}

std::optional<std::filesystem::path> ActiveLogWatcher::findActiveLogFile() const {
    std::optional<std::filesystem::path> mostRecent;
    std::filesystem::file_time_type mostRecentTime;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(logsFolder_)) {
            if (!entry.is_regular_file()) continue;

            if (isCombatLogFile(entry.path().filename())) {
                auto writeTime = entry.last_write_time();
                if (!mostRecent || writeTime > mostRecentTime) {
                    mostRecent = entry.path();
                    mostRecentTime = writeTime;
                }
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        // Ignore errors during directory enumeration
    }

    return mostRecent;
}

bool ActiveLogWatcher::isCombatLogFile(const std::filesystem::path& filename) {
    std::string name = filename.string();

    // WoW combat log files are named: WoWCombatLog-MMDDYY_HHMMSS.txt
    // Or just: WoWCombatLog.txt (older format)
    if (name.size() < 13) return false;

    // Check prefix (case-insensitive)
    std::string prefix = name.substr(0, 12);
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);

    if (prefix != "wowcombatlog") return false;

    // Check extension
    std::string ext = filename.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    return ext == ".txt";
}
