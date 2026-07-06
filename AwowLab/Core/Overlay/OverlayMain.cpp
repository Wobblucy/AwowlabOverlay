// Entry point for the standalone live overlay executable.
// Mirrors AwowLab.cpp's WinMain/main split (console in Debug, windowed in
// Release) but only boots what the overlay needs: settings, localization,
// then OverlayApplication.

#include "OverlayApplication.h"
#include "EmbeddedOverlayAssets.h"
#include "Core/ErrorLogger.h"
#include "Core/LocalizationManager.h"
#include "Core/UnifiedSettings.h"
#include "Core/Utils/ExecutablePath.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>  // For CommandLineToArgvW
#endif

namespace {

// Clamp a saved locale to what the overlay's built-in font can render.
// The language picker only ever offers Latin/Cyrillic locales, but a
// settings file left over from an older build could still carry a CJK
// choice, which would render as empty boxes - fall those back to English.
Locale overlayDisplayableLocale(Locale locale) {
    switch (locale) {
        case Locale::ko_KR:
        case Locale::zh_CN:
        case Locale::zh_TW:
            return Locale::en_US;
        default:
            return locale;
    }
}

// Set working directory to the executable directory so the settings file
// resolves regardless of how the overlay was launched (shortcut, task
// scheduler, etc.)
void setWorkingDirectoryToExe() {
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::string exePath(path);
        size_t lastSlash = exePath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            std::string exeDir = exePath.substr(0, lastSlash);
            SetCurrentDirectoryA(exeDir.c_str());
        }
    }
#endif
}

// Same global setup Application's constructor performs before the overlay
// can run: settings cache, error log, localization. No spell name database:
// the overlay shows the names carried in the combat log itself.
void initializeRuntime() {
    // Initialize the settings cache (loads from disk, migrates legacy if needed)
    SettingsCache::instance().initialize();

    // Initialize error logger (log file goes next to executable)
    ErrorLogger::instance().initialize(awow::getExecutableDirectory());

    // Initialize localization (the overlay UI uses L() strings). The
    // language files are compiled into the executable, so the overlay
    // runs as a single file with no data folder next to it.
    auto& locMgr = LocalizationManager::instance();
    std::vector<LocalizationManager::LocaleData> locales;
    locales.reserve(awow::embedded::kLocaleCsvCount);
    for (size_t i = 0; i < awow::embedded::kLocaleCsvCount; ++i) {
        const auto& csv = awow::embedded::kLocaleCsvs[i];
        locales.push_back({
            csv.localeCode,
            std::string_view(reinterpret_cast<const char*>(csv.data), csv.size)});
    }
    if (!locMgr.loadTranslationsFromMemory(locales)) {
        AWOW_LOG_WARNING("LOCALE", "Built-in language data failed to load - using English keys as fallback");
    }

    // Apply the locale saved in the shared settings, clamped to what the
    // built-in font can render. This is the only source of the overlay's
    // UI language now - the user picks it in the Settings popup, and it
    // persists here. Defaults to en_US when nothing is saved.
    const auto& settings = SettingsCache::instance().get();
    if (!settings.locale.empty()) {
        auto savedLocale = LocalizationManager::parseLocale(settings.locale);
        if (savedLocale) {
            locMgr.setLocale(overlayDisplayableLocale(*savedLocale));
        }
    }
}

// Run the overlay with an optional logs folder from the command line.
// When no folder is given, OverlayApplication falls back to the saved
// folder or prompts with a folder picker.
int runOverlay(const std::filesystem::path& logsFolder) {
    try {
        OverlayApplication overlay;
        overlay.setUiFont(awow::embedded::kUiFont, awow::embedded::kUiFontSize);
        if (!logsFolder.empty()) {
            overlay.setLogsFolder(logsFolder);
        }
        int result = overlay.run();
        AWOW_LOG_INFO("OVERLAY", "Overlay exited with code: " + std::to_string(result));
        return result;
    } catch (const std::exception& e) {
        AWOW_LOG_ERROR("OVERLAY", std::string("Overlay crashed with exception: ") + e.what());
        ErrorLogger::instance().logCrashUnsafe(std::string("Unhandled C++ exception: ") + e.what());
        return -1;
    } catch (...) {
        AWOW_LOG_ERROR("OVERLAY", "Overlay crashed with unknown exception");
        ErrorLogger::instance().logCrashUnsafe("Unhandled unknown exception");
        return -2;
    }
}

// Accept the logs folder either as "--logs <path>" / "-l <path>" or as a
// bare first argument pointing at an existing directory.
template <typename CharT>
std::filesystem::path logsFolderFromCommandLine(int argc, CharT* argv[]) {
    std::filesystem::path folder = getLogsFolderFromArgs(argc, argv);
    if (folder.empty() && argc >= 2) {
        std::filesystem::path candidate(argv[1]);
        std::error_code ec;
        if (std::filesystem::is_directory(candidate, ec)) {
            folder = candidate;
        }
    }
    return folder;
}

}  // namespace

#if defined(_WIN32) && defined(NDEBUG)
// Windows Release: Use WinMain to avoid console window

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    setWorkingDirectoryToExe();
    initializeRuntime();

    std::filesystem::path logsFolder;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        logsFolder = logsFolderFromCommandLine(argc, argv);
        LocalFree(argv);
    }

    return runOverlay(logsFolder);
}

#else
// Windows Debug or Linux/macOS: Use standard main()

int main(int argc, char* argv[]) {
    setWorkingDirectoryToExe();
    initializeRuntime();

    return runOverlay(logsFolderFromCommandLine(argc, argv));
}

#endif
