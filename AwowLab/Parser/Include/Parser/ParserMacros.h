#pragma once
#include "ParserDebug.h"

// =============================================================================
// Parser Validation and Debug Macros
// These macros consolidate the common validation and debug patterns used
// across all parser specializations.
// =============================================================================

namespace parser {

// -----------------------------------------------------------------------------
// Token Validation Macros
// -----------------------------------------------------------------------------

// Validate token count and return early if insufficient
// Usage: PARSER_VALIDATE_TOKENS(tokens, expected_count, data)
#define PARSER_VALIDATE_TOKENS(tokens, expected_count, data) \
    if ((tokens).size() < (expected_count)) { \
        PARSER_RECORD_FAILURE(tokens, expected_count); \
        return data; \
    }

// Validate token count with custom event name for debug output
// Usage: PARSER_VALIDATE_TOKENS_NAMED("EVENT_NAME", tokens, expected_count, data)
#define PARSER_VALIDATE_TOKENS_NAMED(event_name, tokens, expected_count, data) \
    if ((tokens).size() < (expected_count)) { \
        PARSER_RECORD_FAILURE_NAMED(event_name, tokens); \
        return data; \
    }

// -----------------------------------------------------------------------------
// Debug Recording Macros (conditionally compiled)
// -----------------------------------------------------------------------------

#ifndef NDEBUG

// Record parse failure with default GUID extraction
#define PARSER_RECORD_FAILURE(tokens, expected_count) \
    debug::recordParseFailure(__func__, \
        (tokens).size() > 3 ? (tokens)[3] : "", \
        (tokens).size(), tokens)

// Record parse failure with explicit event name
#define PARSER_RECORD_FAILURE_NAMED(event_name, tokens) \
    debug::recordParseFailure(event_name, \
        (tokens).size() > 3 ? (tokens)[3] : "", \
        (tokens).size(), tokens)

// Record successful token count observation
#define PARSER_RECORD_TOKEN_COUNT(event_name, tokens) \
    debug::recordTokenCount(event_name, (tokens).size())

// Full debug finalization with token count recording
#define PARSER_DEBUG_FINALIZE(event_name, tokens) \
    debug::recordTokenCount(event_name, (tokens).size())

#else

// No-op in release builds
#define PARSER_RECORD_FAILURE(tokens, expected_count) ((void)0)
#define PARSER_RECORD_FAILURE_NAMED(event_name, tokens) ((void)0)
#define PARSER_RECORD_TOKEN_COUNT(event_name, tokens) ((void)0)
#define PARSER_DEBUG_FINALIZE(event_name, tokens) ((void)0)

#endif // NDEBUG

// -----------------------------------------------------------------------------
// Combined Validation + Debug Macro
// Use at the start of parse_and_return() methods
// -----------------------------------------------------------------------------

// Validates tokens and records failure if insufficient, returns data early
// This is the primary macro for use in parser templates
#ifndef NDEBUG
#define PARSER_VALIDATE(event_name, tokens, expected_count, data) \
    if ((tokens).size() < (expected_count)) { \
        debug::recordParseFailure(event_name, \
            (tokens).size() > 3 ? (tokens)[3] : "", \
            (tokens).size(), tokens); \
        return data; \
    }
#else
#define PARSER_VALIDATE(event_name, tokens, expected_count, data) \
    if ((tokens).size() < (expected_count)) { \
        return data; \
    }
#endif

// -----------------------------------------------------------------------------
// Debug Output Finalization
// Use at the end of parse_and_return() methods
// -----------------------------------------------------------------------------

// Records token count and optionally logs debug info
// Usage: PARSER_FINALIZE("SPELL_DAMAGE", tokens);
#ifndef NDEBUG
#define PARSER_FINALIZE(event_name, tokens) \
    debug::recordTokenCount(event_name, (tokens).size())
#else
#define PARSER_FINALIZE(event_name, tokens) ((void)0)
#endif

} // namespace parser
