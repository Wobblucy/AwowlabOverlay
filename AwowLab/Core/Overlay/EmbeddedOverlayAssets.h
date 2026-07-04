#pragma once

#include <cstddef>

// Assets compiled into the standalone overlay executable so it ships as a
// single file with no data folder next to it. The byte arrays are produced
// at build time by cmake/embed_overlay_assets.cmake from data/lang/*.csv
// and data/fonts/NotoSans-Regular.ttf (font license: data/fonts/OFL.txt).

namespace awow::embedded {

// One language file: the locale code ("en_US") and the raw CSV bytes.
struct LocaleCsv {
    const char* localeCode;
    const unsigned char* data;
    size_t size;
};

extern const LocaleCsv kLocaleCsvs[];
extern const size_t kLocaleCsvCount;

// Noto Sans Regular: Latin and Cyrillic coverage for the overlay UI.
extern const unsigned char* const kUiFont;
extern const size_t kUiFontSize;

} // namespace awow::embedded
