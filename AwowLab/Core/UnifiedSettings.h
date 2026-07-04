#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

/**
 * Unified settings manager with AES-256-CBC encryption.
 *
 * Consolidates all application settings into a single encrypted file:
 * - Window size/state
 * - UI panel layouts
 * - Actor appearance customizations
 *
 * If the file is missing or corrupt, ALL settings reset to defaults.
 */
class UnifiedSettings {
public:
    /**
     * All application settings in one structure.
     */
    struct AllSettings {
        // Window settings
        int windowWidth = 1600;
        int windowHeight = 900;
        bool windowMaximized = false;

        // UI scaling
        float fontScale = 1.0f;  // Range: 0.5 to 2.0

        // Locale/language setting (e.g., "en_US", "ko_KR")
        std::string locale = "en_US";

        // UI Layout - stored as JSON string for flexibility
        std::string uiLayoutJson;

        // Actor appearance - stored as JSON string
        std::string actorAppearanceJson;

        // Spell grouping settings - stored as JSON string per spec
        std::string spellGroupsJson;

        // Spell assignment settings - per-class display locations
        std::string spellAssignmentJson;

        // Spell data version - "12.0" or "11.2"
        std::string spellDataVersion = "12.0";

        // Nameplate settings - JSON for flexibility
        std::string nameplateSettingsJson;

        // Raid frame config - JSON for flexibility
        std::string raidFrameConfigJson;

        // Boss timer settings - JSON for flexibility
        std::string bossTimerSettingsJson;

        // Facing indicator settings - JSON for flexibility
        std::string facingIndicatorSettingsJson;

        // Mob damage weighting - JSON for flexibility
        std::string mobWeightSettingsJson;

        // Boss phase rules - JSON for flexibility
        std::string phaseSettingsJson;

        // Meter bars use WoW class colors for players whose spec is
        // known (from COMBATANT_INFO); off falls back to the generated
        // per-actor colors
        bool meterClassColors = true;

        // Custom model rotations - simple key=value format
        std::string customModelRotationsJson;

        // Overlay logs folder path (persisted so user doesn't have to pick every time)
        std::string overlayLogsFolder;

        // Top-level file entries not handled by the fields above, kept as
        // raw JSON (entry name -> JSON value text). They are read on load
        // and written back on save, so optional features can keep their
        // own values here and builds with different feature sets can share
        // one settings file without losing each other's data.
        std::map<std::string, std::string> extraValues;
    };

    /**
     * Load all settings from encrypted file.
     * @param settings Output settings structure.
     * @return True if loaded successfully, false if file missing/corrupt (use defaults).
     */
    static bool load(AllSettings& settings);

    /**
     * Save all settings to encrypted file.
     * @param settings Settings to save.
     * @return True if saved successfully.
     */
    static bool save(const AllSettings& settings);

    /**
     * Check if encrypted settings file exists.
     */
    static bool exists();

    /**
     * Delete the settings file (for testing/reset).
     */
    static void clear();

    /**
     * Get the settings file path.
     */
    static std::filesystem::path getFilePath();

private:
    static constexpr const char* FILENAME = "User Settings/settings.dat";
    static constexpr uint32_t MAGIC = 0x574F5741;  // "AWOW" in little-endian
    static constexpr uint32_t VERSION = 1;
    static constexpr size_t IV_SIZE = 16;
    static constexpr size_t KEY_SIZE = 32;  // AES-256

    /**
     * Derive encryption key from hardcoded salt.
     */
    static std::vector<uint8_t> deriveKey();

    /**
     * Encrypt plaintext using AES-256-CBC.
     * @param plaintext Data to encrypt.
     * @param iv Output: randomly generated IV.
     * @return Encrypted ciphertext.
     */
    static std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, std::vector<uint8_t>& iv);

    /**
     * Decrypt ciphertext using AES-256-CBC.
     * @param ciphertext Data to decrypt.
     * @param iv IV used for encryption.
     * @return Decrypted plaintext, or empty on failure.
     */
    static std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& iv);

    /**
     * Serialize settings to JSON string.
     */
    static std::string serializeToJson(const AllSettings& settings);

    /**
     * Deserialize settings from JSON string.
     */
    static bool deserializeFromJson(const std::string& json, AllSettings& settings);
};

/**
 * Optional callbacks the host application can register around settings
 * persistence. The main app uses these to reconcile and mirror some of
 * its values outside the settings file; the standalone overlay registers
 * nothing and works with the file alone.
 */
struct SettingsPersistenceHooks {
    /** Runs at the end of initialize(), after the file has been read.
     *  May adjust the freshly loaded settings in place. */
    std::function<void(UnifiedSettings::AllSettings&)> afterLoad;

    /** Runs after every successful save to disk. */
    std::function<void(const UnifiedSettings::AllSettings&)> afterSave;
};

/**
 * Singleton cache for settings with auto-save functionality.
 *
 * All components should use this instead of directly calling UnifiedSettings::load/save.
 * Settings are cached in memory and auto-saved every 10 seconds if dirty.
 *
 * Usage:
 *   auto& cache = SettingsCache::instance();
 *   cache.get().windowWidth = 1920;  // Modify settings
 *   cache.markDirty();               // Mark for save
 *   cache.tick();                    // Call each frame to check auto-save timer
 */
class SettingsCache {
public:
    static constexpr double AUTO_SAVE_INTERVAL_SECONDS = 10.0;

    /**
     * Get the singleton instance.
     */
    static SettingsCache& instance();

    // Delete copy/move
    SettingsCache(const SettingsCache&) = delete;
    SettingsCache& operator=(const SettingsCache&) = delete;
    SettingsCache(SettingsCache&&) = delete;
    SettingsCache& operator=(SettingsCache&&) = delete;

    /**
     * Register persistence callbacks (see SettingsPersistenceHooks).
     * Call before initialize() so the load step can run them.
     * Without a registration, loads and saves touch only the settings file.
     */
    void setPersistenceHooks(SettingsPersistenceHooks hooks);

    /**
     * Initialize the cache by loading settings from disk.
     * Call once at application startup.
     */
    void initialize();

    /**
     * Get mutable reference to cached settings.
     * Call markDirty() after modifying.
     */
    UnifiedSettings::AllSettings& get() { return settings_; }

    /**
     * Get const reference to cached settings.
     */
    const UnifiedSettings::AllSettings& get() const { return settings_; }

    /**
     * Mark settings as modified (needs save).
     */
    void markDirty();

    /**
     * Check if settings have been modified since last save.
     */
    bool isDirty() const { return dirty_; }

    /**
     * Call each frame to check auto-save timer.
     * Saves to disk if dirty and AUTO_SAVE_INTERVAL_SECONDS has passed.
     */
    void tick();

    /**
     * Force immediate save to disk (if dirty).
     * Call on application exit.
     */
    void flush();

    /**
     * Force save even if not dirty.
     * Use sparingly (e.g., after critical changes).
     */
    void forceSave();

private:
    SettingsCache() = default;
    ~SettingsCache();

    UnifiedSettings::AllSettings settings_;
    SettingsPersistenceHooks hooks_;
    std::atomic<bool> dirty_{false};
    std::chrono::steady_clock::time_point lastSaveTime_;
    bool initialized_{false};
    mutable std::mutex mutex_;
};
