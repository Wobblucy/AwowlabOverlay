#include "LocalizationManager.h"
#include "ErrorLogger.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>

// ============================================================================
// Static locale data
// ============================================================================

namespace {

struct LocaleInfo {
    const char* code;           // "en_US"
    const char* displayName;    // Native language name
    const char* englishName;    // English name
};

constexpr LocaleInfo LOCALE_INFO[] = {
    {"en_US", "English (US)", "English (US)"},
    {"es_MX", "Español (México)", "Spanish (Mexico)"},
    {"pt_BR", "Português (Brasil)", "Portuguese (Brazil)"},
    {"de_DE", "Deutsch", "German"},
    {"fr_FR", "Français", "French"},
    {"ru_RU", "Русский", "Russian"},
    {"ko_KR", "한국어", "Korean"},
    {"zh_TW", "繁體中文", "Chinese (Traditional)"},
    {"zh_CN", "简体中文", "Chinese (Simplified)"},
};

static_assert(std::size(LOCALE_INFO) == static_cast<size_t>(Locale::COUNT),
              "LOCALE_INFO must have entries for all locales");

} // anonymous namespace

// ============================================================================
// Singleton instance
// ============================================================================

LocalizationManager& LocalizationManager::instance() {
    static LocalizationManager instance;
    return instance;
}

// ============================================================================
// Translation loading
// ============================================================================

bool LocalizationManager::loadTranslations(const std::string& langDir) {
    langDir_ = langDir;
    translations_.clear();
    loaded_ = false;

    // Must load en_US as the fallback locale
    std::string enUsPath = langDir + "/en_US.csv";
    if (!loadLocaleFile(enUsPath, Locale::en_US)) {
        // Don't log - Application.cpp logs a summary warning about localization
        return false;
    }

    // Load other locales (optional - missing translations will fall back to en_US)
    for (size_t i = 1; i < static_cast<size_t>(Locale::COUNT); ++i) {
        Locale loc = static_cast<Locale>(i);
        std::string path = langDir + "/" + getLocaleCode(loc) + ".csv";

        if (std::filesystem::exists(path)) {
            if (!loadLocaleFile(path, loc)) {
                // Parse failure for non-primary locale - just use fallback silently
            }
        }
    }

    loaded_ = true;
#ifndef NDEBUG
    std::cout << "LocalizationManager: Loaded " << translations_.size() << " translation keys\n";
#endif
    return true;
}

bool LocalizationManager::reloadCurrentLocale() {
    if (langDir_.empty()) {
        return false;
    }

    // Reload en_US first (fallback)
    std::string enUsPath = langDir_ + "/en_US.csv";
    if (!loadLocaleFile(enUsPath, Locale::en_US)) {
        return false;
    }

    // Reload current locale if different from en_US
    if (currentLocale_ != Locale::en_US) {
        std::string path = langDir_ + "/" + getLocaleCode(currentLocale_) + ".csv";
        if (std::filesystem::exists(path)) {
            loadLocaleFile(path, currentLocale_);
        }
    }

    return true;
}

bool LocalizationManager::loadLocaleFile(const std::string& filepath, Locale locale) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    int lineNum = 0;
    size_t localeIdx = static_cast<size_t>(locale);

    while (std::getline(file, line)) {
        ++lineNum;

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Skip BOM if present (first line only)
        if (lineNum == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line = line.substr(3);
        }

        // Remove carriage return if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Skip header row
        if (lineNum == 1 && line.find("key,") == 0) {
            continue;
        }

        // Parse key,value pair
        auto [key, value] = parseCSVLine(line);
        if (key.empty()) {
            continue;
        }

        // Store translation
        translations_[key][localeIdx] = std::move(value);
    }

    return true;
}

std::pair<std::string, std::string> LocalizationManager::parseCSVLine(const std::string& line) const {
    std::string key;
    std::string value;

    // Find the first comma (key is never quoted)
    size_t commaPos = line.find(',');
    if (commaPos == std::string::npos) {
        return {key, value};
    }

    key = line.substr(0, commaPos);

    // Parse value (may be quoted)
    size_t valueStart = commaPos + 1;
    if (valueStart >= line.size()) {
        return {key, value};
    }

    if (line[valueStart] == '"') {
        // Quoted value - find closing quote
        // Handle escaped quotes ("") within the value
        std::string result;
        size_t pos = valueStart + 1;

        while (pos < line.size()) {
            if (line[pos] == '"') {
                // Check for escaped quote
                if (pos + 1 < line.size() && line[pos + 1] == '"') {
                    result += '"';
                    pos += 2;
                } else {
                    // End of quoted string
                    break;
                }
            } else {
                result += line[pos];
                ++pos;
            }
        }

        value = std::move(result);
    } else {
        // Unquoted value - take rest of line
        value = line.substr(valueStart);
    }

    return {key, value};
}

// ============================================================================
// Locale switching
// ============================================================================

void LocalizationManager::setLocale(Locale locale) {
    if (locale == currentLocale_) {
        return;
    }

    currentLocale_ = locale;

    // Reload current locale file to ensure we have latest translations
    if (!langDir_.empty() && locale != Locale::en_US) {
        std::string path = langDir_ + "/" + getLocaleCode(locale) + ".csv";
        if (std::filesystem::exists(path)) {
            loadLocaleFile(path, locale);
        }
    }

#ifndef NDEBUG
    std::cout << "LocalizationManager: Switched to " << getLocaleCode(locale) << "\n";
#endif
}

// ============================================================================
// Translation lookup
// ============================================================================

const char* LocalizationManager::get(std::string_view key) const {
    // Thread-safe: translations_ is only written during loadTranslations()
    // which happens before UI rendering starts

    auto it = translations_.find(std::string(key));
    if (it == translations_.end()) {
        // Key not found - return key itself for debugging
        // Note: This returns a dangling pointer if key is temporary!
        // For safety, we should store the key and return that.
        // But in practice, keys are always string literals.
        static thread_local std::string fallbackKey;
        fallbackKey = key;
        return fallbackKey.c_str();
    }

    const auto& localeStrings = it->second;
    size_t localeIdx = static_cast<size_t>(currentLocale_);

    // Try current locale
    if (!localeStrings[localeIdx].empty()) {
        return localeStrings[localeIdx].c_str();
    }

    // Fall back to en_US
    if (!localeStrings[0].empty()) {
        return localeStrings[0].c_str();
    }

    // No translation available - return key
    static thread_local std::string fallbackKey2;
    fallbackKey2 = key;
    return fallbackKey2.c_str();
}

bool LocalizationManager::hasKey(std::string_view key) const {
    return translations_.find(std::string(key)) != translations_.end();
}

// ============================================================================
// Static locale utilities
// ============================================================================

const char* LocalizationManager::getLocaleCode(Locale locale) {
    size_t idx = static_cast<size_t>(locale);
    if (idx < std::size(LOCALE_INFO)) {
        return LOCALE_INFO[idx].code;
    }
    return "en_US";
}

const char* LocalizationManager::getLocaleDisplayName(Locale locale) {
    size_t idx = static_cast<size_t>(locale);
    if (idx < std::size(LOCALE_INFO)) {
        return LOCALE_INFO[idx].displayName;
    }
    return "English (US)";
}

const char* LocalizationManager::getLocaleEnglishName(Locale locale) {
    size_t idx = static_cast<size_t>(locale);
    if (idx < std::size(LOCALE_INFO)) {
        return LOCALE_INFO[idx].englishName;
    }
    return "English (US)";
}

std::optional<Locale> LocalizationManager::parseLocale(std::string_view code) {
    for (size_t i = 0; i < std::size(LOCALE_INFO); ++i) {
        if (code == LOCALE_INFO[i].code) {
            return static_cast<Locale>(i);
        }
    }
    return std::nullopt;
}

