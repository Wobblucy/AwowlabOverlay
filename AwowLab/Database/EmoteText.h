#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// Emote line helpers shared by the offline extractor (OutputWriter) and
// the live overlay session. EMOTE lines skip the usual flag fields:
//   EMOTE,srcGuid,"SrcName",destGuid,"DestName",<free text>
// The text is unquoted, so the tokenizer splits it on spaces and commas
// - everything from token 7 on is the emote body.
namespace awow::emote {

// First token index of the emote body
inline constexpr size_t kTextStartToken = 7;

// Strip WoW UI escape sequences from emote text: |T...|t textures,
// |cAARRGGBB color starts, |r color resets, and |H...|h spell-link
// wrappers (the [Spell Name] between them stays). Produces the plain
// sentence a player reads on screen.
inline std::string cleanEmoteText(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());

    size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] == '|' && i + 1 < raw.size()) {
            char code = raw[i + 1];
            if (code == 'T') {
                size_t end = raw.find("|t", i + 2);
                i = (end == std::string_view::npos) ? raw.size() : end + 2;
                continue;
            }
            if (code == 'c') {
                i += (std::min)(static_cast<size_t>(10), raw.size() - i);
                continue;
            }
            if (code == 'H') {
                size_t end = raw.find("|h", i + 2);
                i = (end == std::string_view::npos) ? raw.size() : end + 2;
                continue;
            }
            if (code == 'r' || code == 'h') {
                i += 2;
                continue;
            }
        }
        out.push_back(raw[i]);
        ++i;
    }

    // Trim the leftovers texture-stripping leaves at the edges
    size_t begin = out.find_first_not_of(' ');
    size_t last = out.find_last_not_of(' ');
    if (begin == std::string::npos) {
        return {};
    }
    return out.substr(begin, last - begin + 1);
}

// Rebuild the emote body from the split tokens (joined with spaces),
// then strip the UI escape sequences so the same emote reads
// identically on every pull. Empty when the line carries no body.
inline std::string emoteTextFromTokens(const std::vector<std::string_view>& tokens) {
    std::string rawText;
    for (size_t i = kTextStartToken; i < tokens.size(); ++i) {
        if (!rawText.empty()) rawText.push_back(' ');
        rawText.append(tokens[i]);
    }
    return cleanEmoteText(rawText);
}

} // namespace awow::emote
