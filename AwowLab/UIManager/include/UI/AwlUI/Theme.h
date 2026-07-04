#pragma once

#include <imgui.h>

// AwlUI - AwowLab's thin custom widget layer over Dear ImGui.
//
// This header centralizes design tokens (spacing, radii, colors,
// font handles) that awlui::Button, awlui::Combo, and the other
// widget renderers use to paint themselves. It's the single source
// of truth: adjust a value here and every custom widget picks it up.
//
// ============================================================
//  ACCENT COLOR IS RESERVED.
// ============================================================
// The amber accent is only used for states that need to signal
// primary action or focus:
//
//   - awlui::Button(Primary)          fills with Accent
//   - active TabBar underline         2px Accent
//   - Checkbox / Toggle "on" state    fill Accent
//   - Slider fill up to grab          Accent
//   - keyboard focus outline          NavHighlight (== Accent)
//   - text selection highlight        TextSelectedBg (Accent, low alpha)
//   - plot data series                Accent gradient
//
// Everything else - hover, secondary buttons, unpressed combos,
// idle tabs, section dividers - stays on the neutral Surface*
// palette so the accent has room to breathe. If a new widget wants
// to reach for amber, first ask whether the state it's expressing
// is one of the categories above. If not, use Surface2 for hover
// and Surface3 for pressed, same as everything else.
//
// The base ImGui theme in ImGuiManager.cpp also references these
// tokens for the built-in widgets we haven't migrated yet, so an
// unmigrated Combo idle-state matches a migrated Button idle-state.
namespace awlui { namespace theme {

// ==================== Spacing (4 / 8 / 16 rhythm) ====================
inline constexpr float SpaceXS = 4.0f;
inline constexpr float SpaceS  = 8.0f;
inline constexpr float SpaceM  = 12.0f;
inline constexpr float SpaceL  = 16.0f;
inline constexpr float SpaceXL = 24.0f;

// ==================== Radii ====================
inline constexpr float RadiusCard    = 8.0f;
inline constexpr float RadiusControl = 6.0f;
inline constexpr float RadiusPill    = 999.0f;   // sentinel; widgets use height/2

// ==================== Sizing ====================
inline constexpr float ControlHeight       = 28.0f;
inline constexpr float ButtonPadX          = 14.0f;
inline constexpr float SectionHeaderHeight = 32.0f;
inline constexpr float TabHeight           = 32.0f;
inline constexpr float CheckboxBox         = 16.0f;

// ==================== Borders ====================
inline constexpr float BorderThin = 1.0f;

// ==================== Colors ====================
// Surface palette. Layered from page (0) to strongly-lifted (3).
inline constexpr ImVec4 Surface0 = ImVec4(0.055f, 0.058f, 0.065f, 1.00f);
inline constexpr ImVec4 Surface1 = ImVec4(0.085f, 0.090f, 0.100f, 1.00f);
inline constexpr ImVec4 Surface2 = ImVec4(0.115f, 0.122f, 0.135f, 1.00f);
inline constexpr ImVec4 Surface3 = ImVec4(0.155f, 0.163f, 0.180f, 1.00f);

inline constexpr ImVec4 BorderSubtle = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
inline constexpr ImVec4 BorderStrong = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);

inline constexpr ImVec4 TextPrimary   = ImVec4(0.94f, 0.95f, 0.97f, 1.00f);
inline constexpr ImVec4 TextSecondary = ImVec4(0.65f, 0.68f, 0.73f, 1.00f);
inline constexpr ImVec4 TextMuted     = ImVec4(0.45f, 0.48f, 0.52f, 1.00f);

// Accent - reserved (see top-of-file rules).
inline constexpr ImVec4 Accent        = ImVec4(0.98f, 0.68f, 0.20f, 1.00f);
inline constexpr ImVec4 AccentHover   = ImVec4(1.00f, 0.75f, 0.28f, 1.00f);
inline constexpr ImVec4 AccentActive  = ImVec4(1.00f, 0.82f, 0.35f, 1.00f);
inline constexpr ImVec4 AccentSubtle  = ImVec4(0.98f, 0.68f, 0.20f, 0.14f);

// Semantic - use for status / danger, not accent.
inline constexpr ImVec4 Success = ImVec4(0.35f, 0.80f, 0.55f, 1.00f);
inline constexpr ImVec4 Warn    = ImVec4(0.95f, 0.75f, 0.30f, 1.00f);
inline constexpr ImVec4 Danger  = ImVec4(0.90f, 0.35f, 0.35f, 1.00f);

// ==================== Font handles ====================
// Populated by CaptureFonts() at end of ImGui init. Nullptr-safe:
// widget code that does PushFont(gFontMedium) on nullptr is a no-op
// under ImGui, so falling back to the default font is automatic.
inline ImFont* gFontMedium = nullptr;   // semibold body / section headers
inline ImFont* gFontMono   = nullptr;   // monospace for numeric spans

// Call at the end of ImGuiManager::initialize() after every
// AddFontFromFileTTF has landed. Looks up the Medium and Mono fonts
// the initializer just loaded and stores them on the globals above.
// If either wasn't loaded (font file missing on disk, etc), the
// corresponding handle stays nullptr and widget PushFont calls
// silently fall through to the default font.
void CaptureFonts(ImFont* medium, ImFont* mono);

}}  // namespace awlui::theme
