#pragma once

#include <string>

namespace awow {

/**
 * @brief Get the directory containing the executable.
 *
 * Returns the full path to the directory where the executable is located,
 * with a trailing slash/backslash. On failure, returns an empty string.
 *
 * @return std::string Path to executable directory with trailing separator
 */
std::string getExecutableDirectory();

} // namespace awow
