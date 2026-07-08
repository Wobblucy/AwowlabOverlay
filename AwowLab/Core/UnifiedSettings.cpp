#include "UnifiedSettings.h"
#include "ErrorLogger.h"
#include "DefaultDefensives.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>

#include <fstream>
#include <iostream>
#include <cstring>
#include <string_view>
#include <unordered_set>

std::filesystem::path UnifiedSettings::getFilePath() {
    return std::filesystem::path(FILENAME);
}

bool UnifiedSettings::exists() {
    return std::filesystem::exists(getFilePath());
}

std::string UnifiedSettings::serializeToJson(const AllSettings& settings) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    // Window settings
    doc.AddMember("windowWidth", settings.windowWidth, alloc);
    doc.AddMember("windowHeight", settings.windowHeight, alloc);
    doc.AddMember("windowMaximized", settings.windowMaximized, alloc);

    // UI scaling
    doc.AddMember("fontScale", settings.fontScale, alloc);

    // Locale
    doc.AddMember("locale", rapidjson::Value(settings.locale.c_str(), alloc), alloc);

    // UI Layout (stored as nested JSON string)
    doc.AddMember("uiLayoutJson", rapidjson::Value(settings.uiLayoutJson.c_str(), alloc), alloc);

    // Actor appearance (stored as nested JSON string)
    doc.AddMember("actorAppearanceJson", rapidjson::Value(settings.actorAppearanceJson.c_str(), alloc), alloc);

    // Spell grouping (stored as nested JSON string)
    doc.AddMember("spellGroupsJson", rapidjson::Value(settings.spellGroupsJson.c_str(), alloc), alloc);

    // Spell assignment (stored as nested JSON string)
    doc.AddMember("spellAssignmentJson", rapidjson::Value(settings.spellAssignmentJson.c_str(), alloc), alloc);

    // Spell data version
    doc.AddMember("spellDataVersion", rapidjson::Value(settings.spellDataVersion.c_str(), alloc), alloc);

    // Nameplate settings (stored as nested JSON string)
    doc.AddMember("nameplateSettingsJson", rapidjson::Value(settings.nameplateSettingsJson.c_str(), alloc), alloc);

    // Raid frame config (stored as nested JSON string)
    doc.AddMember("raidFrameConfigJson", rapidjson::Value(settings.raidFrameConfigJson.c_str(), alloc), alloc);

    // Boss timer settings (stored as nested JSON string)
    doc.AddMember("bossTimerSettingsJson", rapidjson::Value(settings.bossTimerSettingsJson.c_str(), alloc), alloc);

    // Facing indicator settings (stored as nested JSON string)
    doc.AddMember("facingIndicatorSettingsJson", rapidjson::Value(settings.facingIndicatorSettingsJson.c_str(), alloc), alloc);

    // Mob damage weighting (stored as nested JSON string)
    doc.AddMember("mobWeightSettingsJson", rapidjson::Value(settings.mobWeightSettingsJson.c_str(), alloc), alloc);

    // Boss phase rules (stored as nested JSON string)
    doc.AddMember("phaseSettingsJson", rapidjson::Value(settings.phaseSettingsJson.c_str(), alloc), alloc);

    // Meter bar coloring
    doc.AddMember("meterClassColors", settings.meterClassColors, alloc);

    // Custom model rotations
    doc.AddMember("customModelRotationsJson", rapidjson::Value(settings.customModelRotationsJson.c_str(), alloc), alloc);

    // Overlay logs folder
    doc.AddMember("overlayLogsFolder", rapidjson::Value(settings.overlayLogsFolder.c_str(), alloc), alloc);

    // Tracked defensive spell ids (death-recap "did they press a defensive")
    {
        rapidjson::Value arr(rapidjson::kArrayType);
        for (uint32_t id : settings.trackedDefensiveSpellIds) {
            arr.PushBack(id, alloc);
        }
        doc.AddMember("trackedDefensiveSpellIds", arr, alloc);
    }

    // Entries this build doesn't handle itself - written back verbatim so
    // nothing another build stored here gets lost
    for (const auto& [name, jsonText] : settings.extraValues) {
        rapidjson::Document valueDoc(&alloc);
        if (valueDoc.Parse(jsonText.c_str()).HasParseError()) {
            continue;  // malformed entry - drop rather than corrupt the file
        }
        doc.AddMember(rapidjson::Value(name.c_str(), alloc),
                      rapidjson::Value(valueDoc, alloc), alloc);
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

bool UnifiedSettings::deserializeFromJson(const std::string& json, AllSettings& settings) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());

    if (doc.HasParseError()) {
        AWOW_LOG_WARNING("CONFIG", std::string("Settings file corrupted, using defaults: ") + rapidjson::GetParseError_En(doc.GetParseError()));
        return false;
    }

    if (!doc.IsObject()) {
        return false;
    }

    // Window settings
    if (doc.HasMember("windowWidth") && doc["windowWidth"].IsInt()) {
        settings.windowWidth = doc["windowWidth"].GetInt();
    }
    if (doc.HasMember("windowHeight") && doc["windowHeight"].IsInt()) {
        settings.windowHeight = doc["windowHeight"].GetInt();
    }
    if (doc.HasMember("windowMaximized") && doc["windowMaximized"].IsBool()) {
        settings.windowMaximized = doc["windowMaximized"].GetBool();
    }

    // UI scaling
    if (doc.HasMember("fontScale") && doc["fontScale"].IsFloat()) {
        settings.fontScale = doc["fontScale"].GetFloat();
        // Clamp to valid range
        if (settings.fontScale < 0.5f) settings.fontScale = 0.5f;
        if (settings.fontScale > 2.0f) settings.fontScale = 2.0f;
    }

    // Locale
    if (doc.HasMember("locale") && doc["locale"].IsString()) {
        settings.locale = doc["locale"].GetString();
    }

    // UI Layout
    if (doc.HasMember("uiLayoutJson") && doc["uiLayoutJson"].IsString()) {
        settings.uiLayoutJson = doc["uiLayoutJson"].GetString();
    }

    // Actor appearance
    if (doc.HasMember("actorAppearanceJson") && doc["actorAppearanceJson"].IsString()) {
        settings.actorAppearanceJson = doc["actorAppearanceJson"].GetString();
    }

    // Spell grouping
    if (doc.HasMember("spellGroupsJson") && doc["spellGroupsJson"].IsString()) {
        settings.spellGroupsJson = doc["spellGroupsJson"].GetString();
    }

    // Spell assignment
    if (doc.HasMember("spellAssignmentJson") && doc["spellAssignmentJson"].IsString()) {
        settings.spellAssignmentJson = doc["spellAssignmentJson"].GetString();
    }

    // Spell data version
    if (doc.HasMember("spellDataVersion") && doc["spellDataVersion"].IsString()) {
        settings.spellDataVersion = doc["spellDataVersion"].GetString();
    }

    // Nameplate settings
    if (doc.HasMember("nameplateSettingsJson") && doc["nameplateSettingsJson"].IsString()) {
        settings.nameplateSettingsJson = doc["nameplateSettingsJson"].GetString();
    }

    // Raid frame config
    if (doc.HasMember("raidFrameConfigJson") && doc["raidFrameConfigJson"].IsString()) {
        settings.raidFrameConfigJson = doc["raidFrameConfigJson"].GetString();
    }

    // Boss timer settings
    if (doc.HasMember("bossTimerSettingsJson") && doc["bossTimerSettingsJson"].IsString()) {
        settings.bossTimerSettingsJson = doc["bossTimerSettingsJson"].GetString();
    }

    // Facing indicator settings
    if (doc.HasMember("facingIndicatorSettingsJson") && doc["facingIndicatorSettingsJson"].IsString()) {
        settings.facingIndicatorSettingsJson = doc["facingIndicatorSettingsJson"].GetString();
    }

    // Mob damage weighting
    if (doc.HasMember("mobWeightSettingsJson") && doc["mobWeightSettingsJson"].IsString()) {
        settings.mobWeightSettingsJson = doc["mobWeightSettingsJson"].GetString();
    }

    // Boss phase rules
    if (doc.HasMember("phaseSettingsJson") && doc["phaseSettingsJson"].IsString()) {
        settings.phaseSettingsJson = doc["phaseSettingsJson"].GetString();
    }

    // Meter bar coloring
    if (doc.HasMember("meterClassColors") && doc["meterClassColors"].IsBool()) {
        settings.meterClassColors = doc["meterClassColors"].GetBool();
    }

    // Custom model rotations
    if (doc.HasMember("customModelRotationsJson") && doc["customModelRotationsJson"].IsString()) {
        settings.customModelRotationsJson = doc["customModelRotationsJson"].GetString();
    }

    // Overlay logs folder
    if (doc.HasMember("overlayLogsFolder") && doc["overlayLogsFolder"].IsString()) {
        settings.overlayLogsFolder = doc["overlayLogsFolder"].GetString();
    }

    if (doc.HasMember("trackedDefensiveSpellIds") && doc["trackedDefensiveSpellIds"].IsArray()) {
        settings.trackedDefensiveSpellIds.clear();
        for (const auto& v : doc["trackedDefensiveSpellIds"].GetArray()) {
            if (v.IsUint()) settings.trackedDefensiveSpellIds.push_back(v.GetUint());
        }
    }

    // Keep every entry the fields above didn't claim, as raw JSON. This
    // carries values from optional features (and future versions) through
    // a load/save cycle untouched.
    static const std::unordered_set<std::string_view> knownEntries = {
        "windowWidth", "windowHeight", "windowMaximized",
        "fontScale", "locale",
        "uiLayoutJson", "actorAppearanceJson",
        "spellGroupsJson", "spellAssignmentJson", "spellDataVersion",
        "nameplateSettingsJson", "raidFrameConfigJson",
        "bossTimerSettingsJson", "facingIndicatorSettingsJson",
        "mobWeightSettingsJson", "phaseSettingsJson", "meterClassColors",
        "customModelRotationsJson", "overlayLogsFolder",
        "trackedDefensiveSpellIds",
    };

    settings.extraValues.clear();
    for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
        std::string_view name(it->name.GetString(), it->name.GetStringLength());
        if (knownEntries.contains(name)) {
            continue;
        }
        rapidjson::StringBuffer valueBuffer;
        rapidjson::Writer<rapidjson::StringBuffer> valueWriter(valueBuffer);
        it->value.Accept(valueWriter);
        settings.extraValues[std::string(name)] = valueBuffer.GetString();
    }

    return true;
}

bool UnifiedSettings::load(AllSettings& settings) {
    auto filePath = getFilePath();

    if (!std::filesystem::exists(filePath)) {
        return false;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read entire file
    std::vector<uint8_t> fileData((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
    file.close();

    // Minimum size: magic(4) + version(4) + iv(16) + length(4) + at least 16 bytes encrypted
    if (fileData.size() < 4 + 4 + IV_SIZE + 4 + 16) {
        AWOW_LOG_WARNING("CONFIG", "Settings file corrupted (too small), using defaults");
        return false;
    }

    size_t offset = 0;

    // Read and verify magic
    uint32_t magic;
    std::memcpy(&magic, fileData.data() + offset, 4);
    offset += 4;

    if (magic != MAGIC) {
        AWOW_LOG_WARNING("CONFIG", "Settings file corrupted (invalid header), using defaults");
        return false;
    }

    // Read and check version
    uint32_t version;
    std::memcpy(&version, fileData.data() + offset, 4);
    offset += 4;

    if (version != VERSION) {
        // Version mismatch is expected when updating - just use defaults silently
        return false;
    }

    // Read IV
    std::vector<uint8_t> iv(IV_SIZE);
    std::memcpy(iv.data(), fileData.data() + offset, IV_SIZE);
    offset += IV_SIZE;

    // Read encrypted payload length
    uint32_t encryptedLen;
    std::memcpy(&encryptedLen, fileData.data() + offset, 4);
    offset += 4;

    if (offset + encryptedLen > fileData.size()) {
        AWOW_LOG_WARNING("CONFIG", "Settings file corrupted (truncated), using defaults");
        return false;
    }

    // Read encrypted payload
    std::vector<uint8_t> ciphertext(fileData.begin() + offset, fileData.begin() + offset + encryptedLen);

    // Decrypt
    std::vector<uint8_t> plaintext = decrypt(ciphertext, iv);
    if (plaintext.empty()) {
        AWOW_LOG_WARNING("CONFIG", "Settings decryption failed, using defaults");
        return false;
    }

    // Parse JSON
    std::string json(plaintext.begin(), plaintext.end());
    if (!deserializeFromJson(json, settings)) {
        // deserializeFromJson already logs the error
        return false;
    }

    return true;
}

bool UnifiedSettings::save(const AllSettings& settings) {
    // Ensure directory exists
    auto filePath = getFilePath();
    auto parentDir = filePath.parent_path();

    if (!parentDir.empty() && !std::filesystem::exists(parentDir)) {
        std::filesystem::create_directories(parentDir);
    }

    // Serialize to JSON
    std::string json = serializeToJson(settings);
    std::vector<uint8_t> plaintext(json.begin(), json.end());

    // Encrypt
    std::vector<uint8_t> iv;
    std::vector<uint8_t> ciphertext = encrypt(plaintext, iv);

    if (ciphertext.empty()) {
        AWOW_LOG_ERROR("CONFIG", "Failed to encrypt settings - settings will not be saved");
        return false;
    }

    // Build file
    std::vector<uint8_t> fileData;
    fileData.reserve(4 + 4 + IV_SIZE + 4 + ciphertext.size());

    // Helper to safely append POD types to byte vector
    auto appendPOD = [&fileData](const auto& value) {
        const auto* ptr = reinterpret_cast<const uint8_t*>(&value);
        fileData.insert(fileData.end(), ptr, ptr + sizeof(value));
    };

    // Magic
    appendPOD(MAGIC);

    // Version
    appendPOD(VERSION);

    // IV
    fileData.insert(fileData.end(), iv.begin(), iv.end());

    // Encrypted length
    uint32_t encryptedLen = static_cast<uint32_t>(ciphertext.size());
    appendPOD(encryptedLen);

    // Encrypted payload
    fileData.insert(fileData.end(), ciphertext.begin(), ciphertext.end());

    // Write file
    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        AWOW_LOG_ERROR("CONFIG", "Failed to save settings - check file permissions");
        return false;
    }

    file.write(reinterpret_cast<const char*>(fileData.data()), fileData.size());
    file.close();

    return true;
}

void UnifiedSettings::clear() {
    auto filePath = getFilePath();
    if (std::filesystem::exists(filePath)) {
        std::filesystem::remove(filePath);
    }
}

// ============================================================================
// SettingsCache implementation
// ============================================================================

SettingsCache& SettingsCache::instance() {
    static SettingsCache instance;
    return instance;
}

SettingsCache::~SettingsCache() {
    // Save any pending changes on destruction
    flush();
}

void SettingsCache::setPersistenceHooks(SettingsPersistenceHooks hooks) {
    std::lock_guard<std::mutex> lock(mutex_);
    hooks_ = std::move(hooks);
}

void SettingsCache::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return;
    }

    // Load settings from disk
    [[maybe_unused]] bool loadedFromFile = UnifiedSettings::load(settings_);
#ifndef NDEBUG
    if (loadedFromFile) {
        std::cout << "Settings cache initialized from disk.\n";
    } else {
        std::cout << "Settings cache initialized with defaults.\n";
    }
#endif

    // First run only (no settings file existed): seed the tracked-defensives
    // list with a starter set. A returning user who deliberately cleared the
    // list has a file on disk, so this never re-populates behind their back.
    if (!loadedFromFile && settings_.trackedDefensiveSpellIds.empty()) {
        settings_.trackedDefensiveSpellIds = awow::defaultDefensiveSpellIds();
        dirty_ = true;
    }

    // Let the host application reconcile the freshly loaded settings with
    // whatever it keeps outside the file. No-op when nothing is registered
    // (overlay-only installs).
    if (hooks_.afterLoad) {
        hooks_.afterLoad(settings_);
    }

    lastSaveTime_ = std::chrono::steady_clock::now();
    dirty_ = false;
    initialized_ = true;
}

void SettingsCache::markDirty() {
    dirty_ = true;
}

void SettingsCache::tick() {
    if (!dirty_ || !initialized_) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - lastSaveTime_).count();

    if (elapsed >= AUTO_SAVE_INTERVAL_SECONDS) {
        flush();
    }
}

void SettingsCache::flush() {
    if (!dirty_ || !initialized_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (UnifiedSettings::save(settings_)) {
        dirty_ = false;
        lastSaveTime_ = std::chrono::steady_clock::now();

        if (hooks_.afterSave) {
            hooks_.afterSave(settings_);
        }

#ifndef NDEBUG
        std::cout << "Settings auto-saved.\n";
#endif
    } else {
        AWOW_LOG_WARNING("SETTINGS", "Failed to auto-save settings");
    }
}

void SettingsCache::forceSave() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return;
    }

    if (UnifiedSettings::save(settings_)) {
        dirty_ = false;
        lastSaveTime_ = std::chrono::steady_clock::now();

        if (hooks_.afterSave) {
            hooks_.afterSave(settings_);
        }

#ifndef NDEBUG
        std::cout << "Settings force-saved.\n";
#endif
    } else {
        AWOW_LOG_WARNING("SETTINGS", "Failed to force-save settings");
    }
}
