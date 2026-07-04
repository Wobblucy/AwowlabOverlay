#pragma once
#include "Txtfile.h"
#include <vector>
#include <iostream>
#include <string_view>
#include <string>

// Result of incremental tokenization
struct TokenizeResult {
	std::vector<std::vector<std::string_view>> lines;
	size_t bytes_consumed;  // How many bytes were actually parsed (up to last complete line)
};

class tokenized_segment {

public:
	// Original method - parse entire buffer
	std::vector<std::vector<std::string_view>> tokenize(const char* mmap_pointer, size_t mmap_size);

	// Incremental parsing - parse from offset, return result with bytes consumed
	// This allows tracking progress for refreshing partially-parsed files
	TokenizeResult tokenizeIncremental(const char* mmap_pointer, size_t mmap_size, size_t start_offset = 0);

	const char* token_start = nullptr;
	const char* current = nullptr;
	std::vector<std::string_view> tokens;

	void begin_token();
	void append_token();
	void finalize_token(std::vector<std::string_view>& line_tokens);
	void discard_token();
	void print_tokens() const;

private:


}; 