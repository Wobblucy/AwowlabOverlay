#include "ClientLocale.h"

#include <fstream>
#include <iterator>

namespace awow {

namespace {

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

struct ClientLocaleEntry {
    const char* clientCode;
    Locale locale;
};

constexpr ClientLocaleEntry kClientLocaleMap[] = {
    {"enUS", Locale::en_US},
    {"enGB", Locale::en_US},
    {"deDE", Locale::de_DE},
    {"frFR", Locale::fr_FR},
    {"esES", Locale::es_MX},
    {"esMX", Locale::es_MX},
    {"ptBR", Locale::pt_BR},
    {"ptPT", Locale::pt_BR},
    {"ruRU", Locale::ru_RU},
    // Korean and Chinese clients get English for now: the overlay's
    // built-in font carries no CJK glyphs.
    {"koKR", Locale::en_US},
    {"zhCN", Locale::en_US},
    {"zhTW", Locale::en_US},
};

} // anonymous namespace

std::filesystem::path clientConfigPathForLogs(const std::filesystem::path& logsFolder) {
    // "C:/.../_retail_/Logs/" (trailing separator) has an empty filename;
    // strip it so parent_path() steps up to the branch folder.
    std::filesystem::path folder = logsFolder;
    if (folder.filename().empty()) {
        folder = folder.parent_path();
    }
    return folder.parent_path() / "WTF" / "Config.wtf";
}

std::optional<std::string> textLocaleFromConfig(std::string_view configText) {
    size_t pos = 0;
    while (pos < configText.size()) {
        size_t eol = configText.find('\n', pos);
        std::string_view line = (eol == std::string_view::npos)
            ? configText.substr(pos)
            : configText.substr(pos, eol - pos);
        pos = (eol == std::string_view::npos) ? configText.size() : eol + 1;

        // Expected shape: SET textLocale "enUS"
        line = trimmed(line);
        if (!line.starts_with("SET")) {
            continue;
        }
        line = trimmed(line.substr(3));

        constexpr std::string_view kVariable = "textLocale";
        if (!line.starts_with(kVariable)) {
            continue;
        }
        line = trimmed(line.substr(kVariable.size()));

        // The quote must come right after the variable name, which rules
        // out other variables that merely start with "textLocale".
        if (line.empty() || line.front() != '"') {
            continue;
        }
        line.remove_prefix(1);
        size_t closing = line.find('"');
        if (closing == std::string_view::npos || closing == 0) {
            continue;
        }
        return std::string(line.substr(0, closing));
    }
    return std::nullopt;
}

Locale localeForClientCode(std::string_view clientCode) {
    for (const auto& entry : kClientLocaleMap) {
        if (clientCode == entry.clientCode) {
            return entry.locale;
        }
    }
    return Locale::en_US;
}

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

std::optional<Locale> detectClientLocale(const std::filesystem::path& logsFolder) {
    if (logsFolder.empty()) {
        return std::nullopt;
    }

    std::filesystem::path configPath = clientConfigPathForLogs(logsFolder);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(configPath, ec)) {
        return std::nullopt;
    }

    std::ifstream file(configPath, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

    auto code = textLocaleFromConfig(text);
    if (!code) {
        return std::nullopt;
    }
    return localeForClientCode(*code);
}

} // namespace awow
