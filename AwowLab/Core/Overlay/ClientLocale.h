#pragma once

#include "Core/LocalizationManager.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Reads the WoW client's language so the standalone overlay can come up in
// the same language the player sees in game, with no selector of its own.
//
// The client keeps its settings in <branch>/WTF/Config.wtf, a sibling of
// the <branch>/Logs folder the overlay already watches. The language lives
// on a line like:
//
//     SET textLocale "enUS"
//
// Detection is read-only and never persisted: if the player switches their
// client language, the overlay follows on its next launch.

namespace awow {

// Path to the client's Config.wtf for a given Logs folder.
std::filesystem::path clientConfigPathForLogs(const std::filesystem::path& logsFolder);

// Extract the textLocale value ("enUS") from Config.wtf contents.
// Returns nullopt when no usable textLocale line is present.
std::optional<std::string> textLocaleFromConfig(std::string_view configText);

// Map a WoW client locale code to the closest locale the overlay can
// display. Unknown codes map to en_US. Korean and Chinese also map to
// en_US for now: the overlay's built-in font carries no CJK glyphs.
Locale localeForClientCode(std::string_view clientCode);

// Clamp a locale to what the overlay's built-in font can render.
// Latin and Cyrillic locales pass through; CJK locales become en_US.
Locale overlayDisplayableLocale(Locale locale);

// Read Config.wtf next to the Logs folder and map its textLocale.
// Returns nullopt when the file is missing or has no locale line.
std::optional<Locale> detectClientLocale(const std::filesystem::path& logsFolder);

} // namespace awow
