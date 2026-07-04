#pragma once

#include <imgui.h>
#include "UI/AwlUI/Theme.h"

// AwlUI widget API. Custom draws on top of ImGui's interaction
// primitives (InvisibleButton for hit-testing, IsItemHovered/Active
// for state, GetWindowDrawList for pixels). Same pattern as
// AwowLab/UIManager/src/Core/MeterBarRenderer.cpp.
//
// Every widget:
//   - reserves layout via ImGui::Dummy(size) after drawing so
//     ImGui's cursor advances correctly for the next widget,
//   - pushes a unique ID scope with ImGui::PushID / PopID or an
//     "id" arg so nested calls in loops don't collide,
//   - reads focus via ImGui::IsItemFocused() and draws a manual
//     NavHighlight-colored 2px outline for keyboard nav parity
//     with built-in widgets.
namespace awlui {

enum class ButtonVariant {
    Primary,     // reserved-accent fill; use for the CTA on a surface
    Secondary,   // neutral Surface2 fill; use for non-primary actions
    Ghost,       // no fill idle, subtle hover; use for chrome / toggles
    Danger,      // Danger-colored fill; destructive actions
};

enum class ButtonSize {
    Sm,   // 22px height, tighter padding
    Md,   // 28px height (default)
};

// Renders a button matching the awlui visual spec. Returns true on
// click (same semantics as ImGui::Button). If size.x or size.y is 0
// the widget auto-sizes to fit label + padding.
bool Button(const char* label,
            ButtonVariant variant = ButtonVariant::Secondary,
            ButtonSize sizePreset = ButtonSize::Md,
            ImVec2 size = ImVec2(0, 0));

// Convenience for compact icon-only actions (close, lock, chevron).
// `glyph` is a short text string (single letter or unicode) that is
// centered in a square hit region. id is used to disambiguate
// multiple icon buttons on the same row.
bool IconButton(const char* id,
                const char* glyph,
                ButtonVariant variant = ButtonVariant::Ghost);

// 16px square check. Sits on the same row as its label. Returns
// true on the frame the state changes.
bool Checkbox(const char* label, bool* v);

// Pill switch. Visually distinct from Checkbox - use for boolean
// options that toggle a mode (Auto-freeze, Click-through, etc)
// rather than filter-style flags. 32x18 track + 14px knob with a
// short animation on state change.
bool Toggle(const char* label, bool* v);

// 4px-track slider with a circular grab. Track is Surface2, filled
// portion is reserved Accent. Displays label + current value below
// the track. Returns true on the frame the value changes.
bool SliderFloat(const char* label, float* v, float lo, float hi,
                 const char* fmt = "%.2f");
bool SliderInt(const char* label, int* v, int lo, int hi,
               const char* fmt = "%d");

// Combo box. Shape mirrors Button(Secondary) with a right-anchored
// chevron; the popup uses Surface1 bg + BorderSubtle border with
// Surface2 hovered rows. current is 0-based, items is a C array of
// C strings of length count. Returns true when the selection
// changes.
bool Combo(const char* label,
           int* current,
           const char* const items[],
           int count);

// Flat segmented tab strip. Inactive segments are Surface1;
// hovered lift to Surface3; active segments render Surface2 with a
// 2px reserved-Accent underline on their bottom edge. Fits the
// current available content width, split equally across segments.
// Returns true when the selection changes.
bool TabBar(const char* id,
            int* current,
            const char* const items[],
            int count);

// Section header. 32px row with a gFontMedium (semibold) label on
// the left, optional collapsed chevron, and a 1px BorderSubtle
// underline. Callers wrap contents in BeginSection / EndSection
// like the built-in CollapsingHeader. Returns visibility so the
// caller can skip rendering when the section is collapsed.
bool BeginSection(const char* label, bool defaultOpen = true);
void EndSection();

// Numeric span rendered in the monospaced font (gFontMono). Falls
// back to the current font when the mono TTF isn't loaded. Use for
// numbers that need column alignment (DPS, HP percentages, etc).
void TextNumber(const char* fmt, ...);

// Small rotating-arc spinner + inline label. Use to tell the user
// we're crunching (log poll, segment re-parse, etc) so the panel
// does not look frozen while parsingInProgress_ is set. Draws in
// the current cursor row and advances the cursor like TextUnformatted
// would - safe to drop next to other widgets.
void Spinner(const char* label, float radius = 8.0f);

// Centered empty-state block for panels with nothing to show: a big
// muted glyph, a headline, an optional hint line, and an optional
// action button. Centers within the remaining content region.
// Returns true the frame the action button is clicked (always false
// when actionLabel is null). Use instead of a bare "No data" string
// so empty panels tell the user what to do next.
bool EmptyState(const char* id,
                const char* glyph,
                const char* headline,
                const char* hint = nullptr,
                const char* actionLabel = nullptr);

}  // namespace awlui
