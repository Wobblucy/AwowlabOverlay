#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

// Forward declare Locale enum to avoid circular dependency
enum class Locale : uint8_t;

/**
 * Spell name database for localized spell name lookups.
 *
 * Uses lazy loading: only loads spell names for IDs that are actually needed.
 * Supports multiple locales with automatic fallback to English.
 *
 * Usage:
 *   auto& db = SpellNameDatabase::instance();
 *   db.setDataDirectory("data/spellnames");
 *   db.setSpellNameLocale(Locale::ko_KR);  // Optional, defaults to en_US
 *
 *   // After parsing log, load only the spell IDs we need
 *   std::unordered_set<uint32_t> neededIds = {...};
 *   db.loadSpellIds(neededIds);
 *
 *   const char* name = SPELL_NAME(12345);
 */
class SpellNameDatabase {
public:
    /**
     * Get the singleton instance.
     */
    static SpellNameDatabase& instance();

    // Delete copy/move
    SpellNameDatabase(const SpellNameDatabase&) = delete;
    SpellNameDatabase& operator=(const SpellNameDatabase&) = delete;
    SpellNameDatabase(SpellNameDatabase&&) = delete;
    SpellNameDatabase& operator=(SpellNameDatabase&&) = delete;

    /**
     * Set the directory containing spell name CSV files.
     * Files are expected to be named spell_names.csv.gz (en_US) or
     * spell_names_{locale}.csv.gz (other locales).
     *
     * @param dirPath Directory path (e.g., "data/spellnames")
     */
    void setDataDirectory(const std::string& dirPath);

    /**
     * Set the path to the gzip-compressed CSV file.
     * Does NOT load any data - just stores the path for lazy loading.
     * [DEPRECATED] Use setDataDirectory() instead.
     *
     * @param filepath Path to the .csv.gz file
     */
    void setDataPath(const std::string& filepath);

    /**
     * Set the locale for spell names.
     * Clears the cache and will reload spell names on next loadSpellIds() call.
     * Falls back to en_US if the locale file doesn't exist.
     *
     * @param locale The locale to use for spell names
     */
    void setSpellNameLocale(Locale locale);

    /**
     * Get the current spell name locale.
     */
    Locale getSpellNameLocale() const { return currentLocale_; }

    /**
     * Check if a locale file exists.
     *
     * @param locale The locale to check
     * @return True if spell_names_{locale}.csv.gz exists
     */
    bool hasLocaleFile(Locale locale) const;

    /**
     * Load spell names for a specific set of spell IDs (lazy loading).
     * Only decompresses and parses if there are new IDs not already cached.
     * Caches loaded spell names for the lifetime of the application.
     *
     * @param spellIds Set of spell IDs to load names for
     * @return Number of new spell names loaded (0 if all were already cached)
     */
    size_t loadSpellIds(const std::unordered_set<uint32_t>& spellIds);

    /**
     * [DEPRECATED] Load ALL spell names from a gzip-compressed CSV file.
     * Prefer loadSpellIds() for memory efficiency.
     *
     * @param filepath Path to the .csv.gz file
     * @return True if loaded successfully
     */
    bool loadFromGzip(const std::string& filepath);

    /**
     * Get spell name by ID.
     *
     * @param spellId The spell ID to look up
     * @return Spell name, or formatted "Spell {id}" if not found
     */
    const char* getSpellName(uint32_t spellId) const;

    /**
     * Check if a spell name exists in the database.
     */
    bool hasSpellName(uint32_t spellId) const;

    /**
     * Get number of loaded spell names.
     */
    size_t size() const { return spellNames_.size(); }

    /**
     * Check if data path/directory has been set.
     */
    bool hasDataPath() const { return !dataDir_.empty() || !dataPath_.empty(); }

    /**
     * Clear the spell name cache.
     * Call this when switching locales to force a reload.
     */
    void clearCache();

private:
    SpellNameDatabase() = default;

    // Get the effective data path for the current locale
    std::string getEffectiveDataPath() const;

    // Get locale code string (e.g., "ko_KR")
    static const char* getLocaleCode(Locale locale);

    // Decompress gzip data
    bool decompressGzip(const std::string& filepath, std::string& output) const;

    // Parse CSV content and extract only the specified spell IDs
    // Returns the number of new spell names added
    size_t parseCSVForIds(const std::string& content, const std::unordered_set<uint32_t>& targetIds);

    // Parse entire CSV content (for deprecated loadFromGzip)
    bool parseCSV(const std::string& content);

    // Directory containing spell name files
    std::string dataDir_;

    // [DEPRECATED] Direct path to spell name file
    std::string dataPath_;

    // Current locale for spell names
    Locale currentLocale_{};  // Default-initialized, will be set in .cpp

    // Spell ID -> name mapping (cached for app lifetime)
    std::unordered_map<uint32_t, std::string> spellNames_;

    // Cache for formatted fallback strings ("Spell 12345")
    mutable std::unordered_map<uint32_t, std::string> fallbackCache_;
};

// ============================================================================
// Convenience macro for spell name lookup
// ============================================================================

/**
 * Get spell name by ID.
 * Usage: ImGui::Text("%s", SPELL_NAME(spell.spell_id));
 */
#define SPELL_NAME(id) SpellNameDatabase::instance().getSpellName(id)
