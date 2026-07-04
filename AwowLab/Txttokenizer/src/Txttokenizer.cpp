#include "../include/Txttokenizer/Txttokenizer.h"
#include <iostream>

std::vector<std::vector<std::string_view>> tokenized_segment::tokenize(const char* mmap_pointer, size_t mmap_size) {
    // For complete files, include incomplete lines at EOF
    current = mmap_pointer;
    token_start = nullptr;
    tokens.clear();
    bool in_quote = false;
    int space_count = 0;

    std::vector<std::vector<std::string_view>> all_lines_tokenized;
    std::vector<std::string_view> line_tokens;
    line_tokens.reserve(40);  // Combat log lines run 30-40 tokens; skip regrowth

    const char* end = mmap_pointer + mmap_size;

    while (current < end) {
        char c = *current;

        switch (c) {
        case ' ':
            if (!in_quote) {
                space_count++;
                if (space_count <= 3) {
                    finalize_token(line_tokens);
                    discard_token();
                    ++current;
                }
                else {
                    if (token_start == nullptr) begin_token();
                    append_token();
                    ++current;
                }
            }
            else {
                append_token();
                ++current;
            }
            break;

        case '"':
            in_quote = !in_quote;
            if (in_quote) {
                begin_token();
                token_start++;
            }
            else {
                finalize_token(line_tokens);
            }
            ++current;
            break;

        case ',':
            if (!in_quote) {
                finalize_token(line_tokens);
                discard_token();
                ++current;
                break;
            }
            [[fallthrough]];

        case '\n':
            if (!in_quote) {
                finalize_token(line_tokens);
                discard_token();
                if (!line_tokens.empty()) {
                    all_lines_tokenized.push_back(line_tokens);
                    line_tokens.clear();
                }
                space_count = 0;  // Reset space counter for new line
                ++current;
            }
            else {
                append_token();
                ++current;
            }
            break;

        case '\r':
        case '\t':
            if (in_quote) {
                append_token();
            }
            else {
                finalize_token(line_tokens);
                discard_token();
                ++current;
            }
            break;

        default:
            if (!token_start) begin_token();
            append_token();
            break;
        }
    }

    // For complete files, include any trailing incomplete line
    finalize_token(line_tokens);
    if (!line_tokens.empty()) all_lines_tokenized.push_back(line_tokens);

    return all_lines_tokenized;
}

TokenizeResult tokenized_segment::tokenizeIncremental(const char* mmap_pointer, size_t mmap_size, size_t start_offset) {
    TokenizeResult result;
    result.bytes_consumed = 0;

    if (start_offset >= mmap_size) {
        return result;  // Nothing to parse
    }

    current = mmap_pointer + start_offset;
    token_start = nullptr;
    tokens.clear();
    bool in_quote = false;
    int space_count = 0;

    std::vector<std::string_view> line_tokens;
    line_tokens.reserve(40);  // Combat log lines run 30-40 tokens; skip regrowth
    const char* end = mmap_pointer + mmap_size;

    // Track the start of the current line for calculating bytes consumed
    const char* line_start = current;
    const char* last_complete_line_end = current;  // Points to just after last \n

    while (current < end) {
        char c = *current;

        switch (c) {
        case ' ':
            if (!in_quote) {
                space_count++;
                if (space_count <= 3) {
                    finalize_token(line_tokens);
                    discard_token();
                    ++current;
                }
                else {
                    if (token_start == nullptr) begin_token();
                    append_token();
                    ++current;
                }
            }
            else {
                append_token();
                ++current;
            }
            break;

        case '"':
            in_quote = !in_quote;
            if (in_quote) {
                begin_token();
                token_start++;
            }
            else {
                finalize_token(line_tokens);
            }
            ++current;
            break;

        case ',':
            if (!in_quote) {
                finalize_token(line_tokens);
                discard_token();
                ++current;
                break;
            }
            [[fallthrough]];

        case '\n':
            if (!in_quote) {
                finalize_token(line_tokens);
                discard_token();
                if (!line_tokens.empty()) {
                    result.lines.push_back(line_tokens);
                    line_tokens.clear();
                }
                space_count = 0;  // Reset space counter for new line
                ++current;
                // Update last complete line position
                last_complete_line_end = current;
                line_start = current;
            }
            else {
                append_token();
                ++current;
            }
            break;

        case '\r':
        case '\t':
            if (in_quote) {
                append_token();
            }
            else {
                finalize_token(line_tokens);
                discard_token();
                ++current;
            }
            break;

        default:
            if (!token_start) begin_token();
            append_token();
            break;
        }
    }

    // Don't include incomplete lines in the result - they might be still being written
    // Only finalize if we're at the end AND the last character was a newline (complete file)
    // For incremental parsing, we want to stop at the last complete line
    if (!line_tokens.empty()) {
        // There's an incomplete line at the end - don't include it
        // The bytes_consumed will point to the start of this incomplete line
        // so on refresh, we'll re-parse it
    }

    // Calculate bytes consumed (up to the last complete line)
    result.bytes_consumed = static_cast<size_t>(last_complete_line_end - mmap_pointer);

    return result;
}

void tokenized_segment::begin_token() {
    token_start = current;
}

void tokenized_segment::append_token() {
    ++current;
}

void tokenized_segment::finalize_token(std::vector<std::string_view>& line_tokens) {
    if (token_start && current > token_start) {
        line_tokens.emplace_back(token_start, current - token_start);
    }
    token_start = nullptr;
}

void tokenized_segment::discard_token() {
    token_start = nullptr;
}

void tokenized_segment::print_tokens() const {
    for (const auto& token : tokens) {
        std::cout << token << "\n";
    }
}
