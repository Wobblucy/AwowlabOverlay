// pch_base.h - Base precompiled header for non-UI modules (Parser, Database, etc.)
// This file contains STL and Windows headers but NOT Vulkan/ImGui
#pragma once

// Windows - minimize includes
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

// STL (most commonly used across codebase)
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <string>
#include <string_view>
#include <memory>
#include <functional>
#include <optional>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <array>
#include <span>
