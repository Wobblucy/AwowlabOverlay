#include "LiveLogManager.h"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

LiveLogManager::LiveLogManager() = default;

LiveLogManager::~LiveLogManager() {
    stop();
}

bool LiveLogManager::start(const std::filesystem::path& logsFolder) {
    if (running_.load()) {
        return false;  // Already running
    }

    // Verify folder exists
    if (!std::filesystem::exists(logsFolder) || !std::filesystem::is_directory(logsFolder)) {
        if (onError_) {
            onError_("Logs folder does not exist: " + logsFolder.string());
        }
        return false;
    }

    logsFolder_ = logsFolder;
    stopRequested_.store(false);
    pollRequested_.store(false);

    // Create session
    session_ = std::make_unique<LiveLogSession>();

    // Wire up session callbacks
    session_->setOnPullStart([this](const PullSegment& pull) {
        if (onPullStart_) onPullStart_(pull);
    });

    session_->setOnPullEnd([this](const PullSegment& pull) {
        if (onPullEnd_) onPullEnd_(pull);
    });

    session_->setOnDataUpdate([this]() {
        if (onDataUpdate_) onDataUpdate_();
    });

    // Start watching the folder
    if (!watcher_.start(logsFolder)) {
        if (onError_) {
            onError_("Failed to start watching folder: " + logsFolder.string());
        }
        session_.reset();
        return false;
    }

    // Attach to the active log file if one exists WITHOUT running the
    // initial segment scan yet. scanForSegments reads the entire log
    // and on a multi-gig raid log takes several seconds; running it
    // here would block start() -> initLogManager() -> the whole
    // render loop from ever spinning up, and the user would just see
    // a frozen black window. Kick the scan on the poll thread's first
    // iteration instead so the render loop is already ticking, the
    // BELOW_NORMAL priority we set in pollLoop applies, and the
    // meter-panel spinner (driven by parsingInProgress_, which
    // attach flips around the scan) animates the whole time.
    auto activeLog = watcher_.getActiveLogPath();
    if (activeLog) {
        if (!session_->attach(*activeLog, /*scanExisting=*/false)) {
            // Not fatal - we'll attach when a log file appears
        } else {
            pendingInitialScan_.store(true);
        }
    }

    // Start the poll thread
    running_.store(true);
    pollThread_ = std::thread(&LiveLogManager::pollLoop, this);

    return true;
}

void LiveLogManager::stop() {
    if (!running_.load()) {
        return;
    }

    stopRequested_.store(true);

    // Wait for poll thread to finish
    if (pollThread_.joinable()) {
        pollThread_.join();
    }

    running_.store(false);

    // Stop the watcher
    watcher_.stop();

    // Detach session
    if (session_) {
        session_->detach();
    }
}

std::optional<std::filesystem::path> LiveLogManager::getActiveLogPath() const {
    return watcher_.getActiveLogPath();
}

std::chrono::seconds LiveLogManager::getSecondsSinceLastWrite() const {
    return watcher_.getSecondsSinceLastWrite();
}

void LiveLogManager::pollNow() {
    pollRequested_.store(true);
}

void LiveLogManager::pollLoop() {
    // Drop below normal so the OS scheduler always hands WoW's threads
    // the CPU first when we happen to be mid-parse on a mid-fight
    // batch. Without this a big fresh chunk of log bytes (a raid
    // encounter with 10k+ events per second) could measurably lag the
    // game while poll() tokenized and dispatched. BELOW_NORMAL is a
    // one-step drop - still runs, still keeps up on typical loads,
    // but yields the moment WoW asks.
#ifdef _WIN32
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif

    std::filesystem::path lastLogPath;

    // Drain the deferred initial scan first. start() attached the
    // existing log without running scanForSegments so this could
    // happen off the UI thread and the render loop could already be
    // drawing (and animating the spinner) while the scan works. Set
    // lastLogPath so the log-rotation branch below does not re-run
    // handleLogFileChange for the same file.
    if (pendingInitialScan_.exchange(false) && session_ && session_->isAttached()) {
        lastLogPath = session_->getLogPath();
        session_->scanForSegments();
    }

    while (!stopRequested_.load()) {
        // Check if the active log file has changed (new day = new file)
        auto currentLog = watcher_.getActiveLogPath();
        if (currentLog && *currentLog != lastLogPath) {
            handleLogFileChange(*currentLog);
            lastLogPath = *currentLog;
        }

        // Check for new writes or forced poll
        bool shouldPoll = watcher_.hasNewWrite() || pollRequested_.exchange(false);

        if (shouldPoll && session_ && session_->isAttached()) {
            // Poll for new data
            session_->poll();
        }

        // Sleep for the poll interval
        // Use short sleeps to check stop flag more frequently
        auto sleepEnd = std::chrono::steady_clock::now() + pollInterval_;
        while (std::chrono::steady_clock::now() < sleepEnd && !stopRequested_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Check for immediate poll request
            if (pollRequested_.load()) {
                break;
            }
        }
    }
}

void LiveLogManager::handleLogFileChange(const std::filesystem::path& newLogPath) {
    if (!session_) return;

    // If already attached to a different file, we need to decide:
    // - Keep accumulating into same session (for M+ where log may rotate mid-run)
    // - Or start fresh (new raid night)
    // For now, we'll detach and reattach, keeping pull history

    std::filesystem::path currentPath = session_->getLogPath();

    if (currentPath != newLogPath) {
        // New log file detected
        session_->detach();

        if (!session_->attach(newLogPath)) {
            if (onError_) {
                onError_("Failed to attach to log file: " + newLogPath.string());
            }
        }
    }
}
