#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <array>
#include <optional>
#include <span>
#include <iosfwd>
#include <cstdint>

/**
 * Supported WoW locales for UI localization.
 * These match the locales supported by World of Warcraft.
 */
enum class Locale : uint8_t {
    en_US = 0,  // English (US) - Default/fallback
    es_MX,      // Spanish (Mexico)
    pt_BR,      // Portuguese (Brazil)
    de_DE,      // German
    fr_FR,      // French
    ru_RU,      // Russian
    ko_KR,      // Korean
    zh_TW,      // Chinese (Traditional)
    zh_CN,      // Chinese (Simplified)
    COUNT
};

/**
 * Localization manager for UI string translations.
 *
 * Supports runtime locale switching with per-locale CSV files.
 * Falls back to en_US for missing translations.
 *
 * Usage:
 *   auto& loc = LocalizationManager::instance();
 *   loc.loadTranslations("data/lang");
 *   loc.setLocale(Locale::de_DE);
 *
 *   ImGui::Text("%s", L("menu.file"));
 *   auto msg = LF("format.total_dps", totalStr, dpsStr);
 */
class LocalizationManager {
public:
    /**
     * Get the singleton instance.
     */
    static LocalizationManager& instance();

    // Delete copy/move
    LocalizationManager(const LocalizationManager&) = delete;
    LocalizationManager& operator=(const LocalizationManager&) = delete;
    LocalizationManager(LocalizationManager&&) = delete;
    LocalizationManager& operator=(LocalizationManager&&) = delete;

    /**
     * Load translations from per-locale CSV files.
     * Looks for {langDir}/{locale}.csv (e.g., data/lang/en_US.csv)
     *
     * @param langDir Directory containing locale CSV files.
     * @return True if at least en_US.csv loaded successfully.
     */
    bool loadTranslations(const std::string& langDir);

    /**
     * One locale's translations held in memory: the locale code
     * ("en_US") and the full CSV file contents.
     */
    struct LocaleData {
        std::string_view code;
        std::string_view csv;
    };

    /**
     * Load translations from in-memory CSV data instead of files.
     * Used by the standalone overlay, which carries its language files
     * inside the executable. Entries with unknown locale codes are
     * skipped. Uses the same CSV parsing as loadTranslations().
     *
     * @param locales One entry per locale; en_US must be present as the
     *                fallback locale.
     * @return True if the en_US entry was found and loaded.
     */
    bool loadTranslationsFromMemory(std::span<const LocaleData> locales);

    /**
     * Reload translations for current locale.
     * Useful for hot-reloading during development.
     */
    bool reloadCurrentLocale();

    /**
     * Set the current locale for UI display.
     * Clears internal cache to pick up new translations.
     *
     * @param locale New locale to use.
     */
    void setLocale(Locale locale);

    /**
     * Get the current locale.
     */
    Locale getLocale() const { return currentLocale_; }

    /**
     * Get translated string for key.
     * Falls back to en_US if not found in current locale.
     * Returns key itself if not found at all (aids debugging).
     *
     * @param key Translation key (e.g., "menu.file")
     * @return Translated string (as const char* for ImGui compatibility)
     */
    const char* get(std::string_view key) const;

    /**
     * Get translated format string and apply printf-style formatting.
     *
     * @param key Translation key for format string.
     * @param ... Format arguments.
     * @return Formatted string.
     */
    template<typename... Args>
    std::string format(std::string_view key, Args&&... args) const;

    /**
     * Get locale code string (e.g., "en_US", "de_DE").
     */
    static const char* getLocaleCode(Locale locale);

    /**
     * Get locale display name in its native language.
     * (e.g., "Deutsch" for de_DE, "Français" for fr_FR)
     */
    static const char* getLocaleDisplayName(Locale locale);

    /**
     * Get locale display name in English.
     * (e.g., "German" for de_DE)
     */
    static const char* getLocaleEnglishName(Locale locale);

    /**
     * Parse locale from code string.
     * @return Locale enum or nullopt if invalid.
     */
    static std::optional<Locale> parseLocale(std::string_view code);

    /**
     * Check if a translation key exists.
     */
    bool hasKey(std::string_view key) const;

    /**
     * Get number of loaded translation keys.
     */
    size_t keyCount() const { return translations_.size(); }

    /**
     * Check if translations have been loaded.
     */
    bool isLoaded() const { return loaded_; }

private:
    LocalizationManager() = default;

    // Load a single locale CSV file
    bool loadLocaleFile(const std::string& filepath, Locale locale);

    // Load a single locale's CSV data from memory
    bool loadLocaleFromMemory(std::string_view csvText, Locale locale);

    // Shared CSV parsing behind both the file and memory loaders
    bool loadLocaleFromStream(std::istream& in, Locale locale);

    // Parse a single CSV line, handling quoted strings
    std::pair<std::string, std::string> parseCSVLine(const std::string& line) const;

    // Current active locale
    Locale currentLocale_ = Locale::en_US;

    // Translation storage: key -> array[locale] of translations
    // Using std::string for stable storage (string_view would dangle)
    std::unordered_map<std::string, std::array<std::string, static_cast<size_t>(Locale::COUNT)>> translations_;

    // Directory where locale files are stored
    std::string langDir_;

    // Whether translations have been loaded
    bool loaded_ = false;
};

// ============================================================================
// Convenience macros for UI code
// ============================================================================

/**
 * Get translated string.
 * Usage: ImGui::Text("%s", L("menu.file"));
 */
#define L(key) LocalizationManager::instance().get(key)

/**
 * Get formatted translated string.
 * Usage: auto msg = LF("format.total_dps", totalStr, dpsStr);
 */
#define LF(key, ...) LocalizationManager::instance().format(key, __VA_ARGS__)

// ============================================================================
// Template implementation
// ============================================================================

template<typename... Args>
std::string LocalizationManager::format(std::string_view key, Args&&... args) const {
    const char* formatStr = get(key);

    // Calculate required buffer size
    int size = std::snprintf(nullptr, 0, formatStr, std::forward<Args>(args)...);
    if (size <= 0) {
        return std::string(formatStr);
    }

    // Format into buffer
    std::string result(static_cast<size_t>(size) + 1, '\0');
    std::snprintf(result.data(), result.size(), formatStr, std::forward<Args>(args)...);
    result.resize(static_cast<size_t>(size));  // Remove null terminator from size

    return result;
}
