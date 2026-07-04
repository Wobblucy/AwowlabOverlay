// pch.h - Precompiled header for AwowLab
// This file is automatically included in all translation units
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

// Heavy external libraries (compiled once, reused everywhere)
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <GLFW/glfw3.h>
