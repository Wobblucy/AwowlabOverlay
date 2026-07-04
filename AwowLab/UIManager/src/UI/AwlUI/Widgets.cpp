#include "UI/AwlUI/Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace awlui {

namespace T = theme;

namespace {

// Convert ImVec4 to packed ImU32 for AddRectFilled/AddText etc.
ImU32 U32(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }

// Pick the height for a given size preset. Widgets that want an
// explicit size (e.g. Button with size.y != 0) override this.
float sizePresetHeight(ButtonSize sz) {
    switch (sz) {
        case ButtonSize::Sm: return 22.0f;
        case ButtonSize::Md:
        default:             return T::ControlHeight;   // 28
    }
}

float sizePresetPadX(ButtonSize sz) {
    switch (sz) {
        case ButtonSize::Sm: return T::SpaceS + 2.0f;   // 10
        case ButtonSize::Md:
        default:             return T::ButtonPadX;       // 14
    }
}

// Colors for a Button in a given variant + interaction state.
struct ButtonPalette {
    ImVec4 fill;
    ImVec4 text;
    ImVec4 border;   // alpha 0 = no border
};

ButtonPalette buttonPalette(ButtonVariant v, bool hovered, bool active, bool disabled) {
    ButtonPalette p{};
    if (disabled) {
        p.fill   = T::Surface2;
        p.text   = T::TextMuted;
        p.border = ImVec4(0, 0, 0, 0);
        return p;
    }
    switch (v) {
        case ButtonVariant::Primary:
            p.fill   = active ? T::AccentActive : (hovered ? T::AccentHover : T::Accent);
            p.text   = T::Surface0;
            p.border = ImVec4(0, 0, 0, 0);
            break;
        case ButtonVariant::Secondary:
            p.fill   = active ? T::Surface3 : (hovered ? T::Surface3 : T::Surface2);
            p.text   = T::TextPrimary;
            p.border = T::BorderSubtle;
            break;
        case ButtonVariant::Ghost:
            // No fill idle; hover raises a subtle Surface2 at 0.6
            // alpha so the shape reads without dominating.
            if (active)       p.fill = T::Surface3;
            else if (hovered) p.fill = ImVec4(T::Surface2.x, T::Surface2.y, T::Surface2.z, 0.60f);
            else              p.fill = ImVec4(0, 0, 0, 0);
            p.text   = hovered ? T::TextPrimary : T::TextSecondary;
            p.border = ImVec4(0, 0, 0, 0);
            break;
        case ButtonVariant::Danger:
            p.fill   = active
                ? ImVec4(T::Danger.x * 1.10f, T::Danger.y * 1.10f, T::Danger.z * 1.10f, 1.00f)
                : (hovered
                    ? ImVec4(T::Danger.x * 1.05f, T::Danger.y * 1.05f, T::Danger.z * 1.05f, 1.00f)
                    : T::Danger);
            p.text   = T::TextPrimary;
            p.border = ImVec4(0, 0, 0, 0);
            break;
    }
    return p;
}

// Draw a keyboard focus outline that matches the reserved-amber
// NavHighlight in the base theme. Called when IsItemFocused is true
// so custom widgets get the same focus signal as built-ins.
void drawFocusOutline(ImDrawList* draw, ImVec2 min, ImVec2 max, float radius) {
    draw->AddRect(min, max, U32(T::Accent), radius, 0, 2.0f);
}

}  // namespace

bool Button(const char* label, ButtonVariant variant, ButtonSize sizePreset, ImVec2 size) {
    // Disabled state is handled by wrapping call sites in
    // ImGui::BeginDisabled/EndDisabled. That path already suppresses
    // clicks via the item flags system and dims the widget alpha.
    const bool disabled = false;

    // Measure - label size + padding, unless caller forced a size.
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float padX = sizePresetPadX(sizePreset);
    const float minH = sizePresetHeight(sizePreset);
    const float w = size.x > 0 ? size.x : labelSize.x + padX * 2.0f;
    const float h = size.y > 0 ? size.y : std::max(minH, labelSize.y + 8.0f);

    // ImGui interaction primitive gives us hit-testing + IDs.
    const ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
    const ImVec2 rectMin = cursorScreen;
    const ImVec2 rectMax = ImVec2(cursorScreen.x + w, cursorScreen.y + h);

    ImGui::InvisibleButton(label, ImVec2(w, h));

    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused();
    const bool clicked = !disabled && ImGui::IsItemClicked();

    ButtonPalette pal = buttonPalette(variant, hovered, held, disabled);

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Fill.
    if (pal.fill.w > 0.0f) {
        draw->AddRectFilled(rectMin, rectMax, U32(pal.fill), T::RadiusControl);
    }
    // Border (Secondary only draws one).
    if (pal.border.w > 0.0f) {
        draw->AddRect(rectMin, rectMax, U32(pal.border), T::RadiusControl, 0, T::BorderThin);
    }

    // Centered label. Stop at "##" like stock ImGui buttons do - the
    // measurement above already does, so drawing past it would spill
    // the ID suffix outside the button.
    const ImVec2 textPos = ImVec2(
        rectMin.x + (w - labelSize.x) * 0.5f,
        rectMin.y + (h - labelSize.y) * 0.5f);
    draw->AddText(textPos, U32(pal.text), label, std::strstr(label, "##"));

    // Keyboard focus outline (uses reserved amber).
    if (focused && !disabled) {
        drawFocusOutline(draw, rectMin, rectMax, T::RadiusControl);
    }

    return clicked;
}

bool IconButton(const char* id, const char* glyph, ButtonVariant variant) {
    // Disabled state is handled by wrapping call sites in
    // ImGui::BeginDisabled/EndDisabled. That path already suppresses
    // clicks via the item flags system and dims the widget alpha.
    const bool disabled = false;

    // Icon buttons are square, size == ControlHeight.
    const float side = T::ControlHeight;
    const ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
    const ImVec2 rectMin = cursorScreen;
    const ImVec2 rectMax = ImVec2(cursorScreen.x + side, cursorScreen.y + side);

    ImGui::InvisibleButton(id, ImVec2(side, side));

    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused();
    const bool clicked = !disabled && ImGui::IsItemClicked();

    ButtonPalette pal = buttonPalette(variant, hovered, held, disabled);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    if (pal.fill.w > 0.0f) {
        draw->AddRectFilled(rectMin, rectMax, U32(pal.fill), T::RadiusControl);
    }
    if (pal.border.w > 0.0f) {
        draw->AddRect(rectMin, rectMax, U32(pal.border), T::RadiusControl, 0, T::BorderThin);
    }

    // Centered glyph.
    const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
    const ImVec2 textPos = ImVec2(
        rectMin.x + (side - glyphSize.x) * 0.5f,
        rectMin.y + (side - glyphSize.y) * 0.5f);
    draw->AddText(textPos, U32(pal.text), glyph);

    if (focused && !disabled) {
        drawFocusOutline(draw, rectMin, rectMax, T::RadiusControl);
    }

    return clicked;
}

// ============================================================
// Checkbox
// ============================================================
bool Checkbox(const char* label, bool* v) {
    if (!v) return false;

    const float box = T::CheckboxBox;                 // 16
    const float labelPad = T::SpaceS;                 // 8
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    const float rowHeight = std::max(box, labelSize.y);
    const float rowWidth = box + labelPad + labelSize.x;

    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, ImVec2(rowWidth, rowHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    const bool focused = ImGui::IsItemFocused();

    bool changed = false;
    if (clicked) {
        *v = !*v;
        changed = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // The check box itself.
    const ImVec2 boxMin = ImVec2(rowMin.x, rowMin.y + (rowHeight - box) * 0.5f);
    const ImVec2 boxMax = ImVec2(boxMin.x + box, boxMin.y + box);
    const float r = 4.0f;

    ImVec4 boxFill = *v ? T::Accent : (hovered ? T::Surface3 : T::Surface2);
    draw->AddRectFilled(boxMin, boxMax, U32(boxFill), r);
    if (!*v) {
        draw->AddRect(boxMin, boxMax, U32(T::BorderStrong), r, 0, T::BorderThin);
    }

    // Checkmark - hand-drawn 2px polyline for a crisp look at 16px.
    if (*v) {
        const float x0 = boxMin.x + 3.5f, y0 = boxMin.y + 8.0f;
        const float x1 = boxMin.x + 7.0f, y1 = boxMin.y + 11.5f;
        const float x2 = boxMin.x + 12.5f, y2 = boxMin.y + 4.5f;
        const ImVec2 pts[3] = { ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x2, y2) };
        draw->AddPolyline(pts, 3, U32(T::Surface0), 0, 2.0f);
    }

    // Label to the right of the box.
    const ImVec2 textPos = ImVec2(
        rowMin.x + box + labelPad,
        rowMin.y + (rowHeight - labelSize.y) * 0.5f);
    draw->AddText(textPos, U32(T::TextPrimary), label);

    // Focus outline around the whole row.
    if (focused) {
        const ImVec2 rowMax = ImVec2(rowMin.x + rowWidth + 2.0f, rowMin.y + rowHeight);
        drawFocusOutline(draw, ImVec2(rowMin.x - 2.0f, rowMin.y - 1.0f), rowMax, r);
    }

    return changed;
}

// ============================================================
// Toggle - pill switch
// ============================================================
bool Toggle(const char* label, bool* v) {
    if (!v) return false;

    const float trackW = 32.0f;
    const float trackH = 18.0f;
    const float knob   = 14.0f;
    const float labelPad = T::SpaceS;
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    const float rowHeight = std::max(trackH, labelSize.y);
    const float rowWidth = trackW + labelPad + labelSize.x;

    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, ImVec2(rowWidth, rowHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    const bool focused = ImGui::IsItemFocused();

    bool changed = false;
    if (clicked) {
        *v = !*v;
        changed = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();

    const ImVec2 trackMin = ImVec2(rowMin.x, rowMin.y + (rowHeight - trackH) * 0.5f);
    const ImVec2 trackMax = ImVec2(trackMin.x + trackW, trackMin.y + trackH);
    const float trackR = trackH * 0.5f;

    // Track fill.
    ImVec4 trackColor;
    if (*v) trackColor = hovered ? T::AccentHover : T::Accent;
    else    trackColor = hovered ? T::Surface3     : T::Surface2;
    draw->AddRectFilled(trackMin, trackMax, U32(trackColor), trackR);

    // Knob - snapped to on/off position. We could animate this, but
    // the toggle is usually one-shot per interaction so a hard step
    // reads honest.
    const float knobPad = (trackH - knob) * 0.5f;
    const float knobX = *v
        ? trackMax.x - knobPad - knob
        : trackMin.x + knobPad;
    const ImVec2 knobCenter = ImVec2(knobX + knob * 0.5f,
                                      trackMin.y + trackH * 0.5f);
    const ImU32 knobColor = *v ? U32(T::Surface0) : U32(T::TextSecondary);
    draw->AddCircleFilled(knobCenter, knob * 0.5f, knobColor);

    // Label.
    const ImVec2 textPos = ImVec2(
        rowMin.x + trackW + labelPad,
        rowMin.y + (rowHeight - labelSize.y) * 0.5f);
    draw->AddText(textPos, U32(T::TextPrimary), label);

    if (focused) {
        const ImVec2 outMin = ImVec2(rowMin.x - 2.0f, rowMin.y - 1.0f);
        const ImVec2 outMax = ImVec2(rowMin.x + rowWidth + 2.0f, rowMin.y + rowHeight + 1.0f);
        drawFocusOutline(draw, outMin, outMax, trackR);
    }

    return changed;
}

// ============================================================
// Slider (shared internal for float + int variants)
// ============================================================
namespace {

// Draw a slider track + grab and update *value_normalized [0,1] based
// on drag input. Returns true when the value changed.
bool sliderCore(const char* id,
                float* value_normalized,
                ImVec2 size,
                bool* out_focused) {
    if (!value_normalized) return false;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    if (out_focused) *out_focused = ImGui::IsItemFocused();

    bool changed = false;
    if (held) {
        const float mx = ImGui::GetIO().MousePos.x;
        float t = (mx - pos.x) / size.x;
        t = std::clamp(t, 0.0f, 1.0f);
        if (t != *value_normalized) {
            *value_normalized = t;
            changed = true;
        }
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Track (4px, centered vertically).
    const float trackH = 4.0f;
    const float trackY = pos.y + (size.y - trackH) * 0.5f;
    const ImVec2 trackMin = ImVec2(pos.x, trackY);
    const ImVec2 trackMax = ImVec2(pos.x + size.x, trackY + trackH);
    draw->AddRectFilled(trackMin, trackMax, U32(T::Surface2), trackH * 0.5f);

    // Filled portion up to the grab position.
    const float grabX = pos.x + size.x * (*value_normalized);
    draw->AddRectFilled(trackMin, ImVec2(grabX, trackMax.y),
                        U32(T::Accent), trackH * 0.5f);

    // Grab (circle).
    const float grabR = (hovered || held) ? 8.0f : 7.0f;
    const ImVec2 grabCenter = ImVec2(grabX, pos.y + size.y * 0.5f);
    draw->AddCircleFilled(grabCenter, grabR, U32(T::TextPrimary));
    draw->AddCircle(grabCenter, grabR, U32(T::BorderStrong), 0, 1.5f);

    return changed;
}

}  // namespace

bool SliderFloat(const char* label, float* v, float lo, float hi, const char* fmt) {
    if (!v) return false;

    // Layout: label + value on top row, slider below.
    ImGui::PushID(label);

    ImGui::TextColored(T::TextSecondary, "%s", label);
    ImGui::SameLine();

    char valueBuf[64];
    std::snprintf(valueBuf, sizeof(valueBuf), fmt, *v);
    ImVec2 valueSize = ImGui::CalcTextSize(valueBuf);
    // Right-align the value.
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, avail - valueSize.x));
    ImGui::TextUnformatted(valueBuf);

    // The slider itself.
    const float sliderH = 20.0f;
    const float span = std::max(1e-6f, hi - lo);
    float normalized = std::clamp((*v - lo) / span, 0.0f, 1.0f);
    bool focused = false;
    bool changed = sliderCore("##slider",
        &normalized,
        ImVec2(ImGui::GetContentRegionAvail().x, sliderH),
        &focused);
    if (changed) {
        *v = lo + normalized * span;
    }

    if (focused) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetItemRectMin();
        const ImVec2 posMax = ImGui::GetItemRectMax();
        drawFocusOutline(draw,
            ImVec2(pos.x - 2.0f, pos.y - 2.0f),
            ImVec2(posMax.x + 2.0f, posMax.y + 2.0f),
            4.0f);
    }

    ImGui::PopID();
    return changed;
}

bool SliderInt(const char* label, int* v, int lo, int hi, const char* fmt) {
    if (!v) return false;
    // Format the integer through the caller's format spec (e.g. "%d%%") so
    // SliderFloat's overlay text matches what the caller expects. If the
    // caller passes a float spec like "%.2f" we still work because SliderFloat
    // ultimately consumes fmt via snprintf(float).
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), fmt ? fmt : "%d", *v);
    float fv = static_cast<float>(*v);
    // Reuse SliderFloat for the track drawing but pass an already-rendered
    // overlay format so it prints the int correctly.
    bool changed = SliderFloat(label, &fv,
                               static_cast<float>(lo),
                               static_cast<float>(hi), overlay);
    if (changed) {
        *v = static_cast<int>(fv + (fv >= 0.0f ? 0.5f : -0.5f));
    }
    return changed;
}

// ============================================================
// Combo
// ============================================================
bool Combo(const char* label, int* current, const char* const items[], int count) {
    if (!current || !items || count <= 0) return false;

    ImGui::PushID(label);

    // Measure - want a Secondary-Button shape wide enough for the
    // current label + chevron.
    const int cur = std::clamp(*current, 0, count - 1);
    const char* currentText = items[cur] ? items[cur] : "";
    const ImVec2 labelSize = ImGui::CalcTextSize(currentText, nullptr, true);
    const float padX = T::ButtonPadX;
    const float h = T::ControlHeight;
    const float w = std::max(labelSize.x + padX * 2.0f + 20.0f, 120.0f);

    // We route interaction through ImGui's popup system so keyboard
    // nav and click-outside-to-close work correctly. The visible
    // "button" is a custom InvisibleButton + draw list.
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 rectMax = ImVec2(pos.x + w, pos.y + h);

    ImGui::InvisibleButton("##combo", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused();
    const bool clicked = ImGui::IsItemClicked();

    if (clicked) {
        ImGui::OpenPopup("##combo_popup");
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4 fill = held ? T::Surface3 : (hovered ? T::Surface3 : T::Surface2);
    draw->AddRectFilled(pos, rectMax, U32(fill), T::RadiusControl);
    draw->AddRect(pos, rectMax, U32(T::BorderSubtle), T::RadiusControl, 0, T::BorderThin);

    // Current selection text.
    const ImVec2 textPos = ImVec2(pos.x + padX,
                                   pos.y + (h - labelSize.y) * 0.5f);
    draw->AddText(textPos, U32(T::TextPrimary), currentText);

    // Chevron - down-pointing triangle on the right.
    const float chevSize = 4.0f;
    const ImVec2 chevCenter = ImVec2(rectMax.x - padX,
                                      pos.y + h * 0.5f);
    const ImVec2 c0 = ImVec2(chevCenter.x - chevSize, chevCenter.y - chevSize * 0.5f);
    const ImVec2 c1 = ImVec2(chevCenter.x + chevSize, chevCenter.y - chevSize * 0.5f);
    const ImVec2 c2 = ImVec2(chevCenter.x,             chevCenter.y + chevSize * 0.75f);
    draw->AddTriangleFilled(c0, c1, c2, U32(T::TextSecondary));

    if (focused) {
        drawFocusOutline(draw, pos, rectMax, T::RadiusControl);
    }

    // Popup contents. ImGui composes it inside a real window so
    // border/rounding come from the theme's PopupBg/PopupRounding.
    bool changed = false;
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
    if (ImGui::BeginPopup("##combo_popup")) {
        for (int i = 0; i < count; ++i) {
            const bool isSelected = (i == cur);
            const char* item = items[i] ? items[i] : "";
            if (ImGui::Selectable(item, isSelected)) {
                *current = i;
                changed = (i != cur);
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndPopup();
    }

    // The visible label sits to the right when caller supplied one
    // that isn't a hash-hidden identifier ("##foo" == hidden).
    if (label && label[0] != '\0' && !(label[0] == '#' && label[1] == '#')) {
        ImGui::SameLine();
        ImGui::TextColored(T::TextSecondary, "%s", label);
    }

    ImGui::PopID();
    return changed;
}

// ============================================================
// TabBar - flat segmented control
// ============================================================
bool TabBar(const char* id, int* current, const char* const items[], int count) {
    if (!current || !items || count <= 0) return false;

    ImGui::PushID(id);

    const float h = T::TabHeight;
    const float availWidth = ImGui::GetContentRegionAvail().x;
    const float segW = availWidth / static_cast<float>(count);

    const ImVec2 stripMin = ImGui::GetCursorScreenPos();

    // Reserve the whole strip's layout.
    ImGui::InvisibleButton("##strip", ImVec2(availWidth, h));
    // Restore cursor so per-segment hit tests can advance.
    ImGui::SetCursorScreenPos(stripMin);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    bool changed = false;

    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        const ImVec2 segMin = ImVec2(stripMin.x + segW * i, stripMin.y);
        const ImVec2 segMax = ImVec2(segMin.x + segW, segMin.y + h);
        const bool isActive = (i == *current);

        // Hit test per segment.
        ImGui::SetCursorScreenPos(segMin);
        ImGui::InvisibleButton("##seg", ImVec2(segW, h));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();

        if (clicked && !isActive) {
            *current = i;
            changed = true;
        }

        // Fill.
        ImVec4 fill;
        if (isActive)      fill = T::Surface2;
        else if (hovered)  fill = T::Surface3;
        else               fill = T::Surface1;
        draw->AddRectFilled(segMin, segMax, U32(fill));

        // Reserved-Accent underline on the active segment.
        if (isActive) {
            const float ulH = 2.0f;
            draw->AddRectFilled(
                ImVec2(segMin.x, segMax.y - ulH),
                segMax,
                U32(T::Accent));
        }

        // Centered label.
        const char* item = items[i] ? items[i] : "";
        const ImVec2 lsz = ImGui::CalcTextSize(item);
        const ImU32 textCol = isActive ? U32(T::TextPrimary) : U32(T::TextSecondary);
        const ImVec2 textPos = ImVec2(
            segMin.x + (segW - lsz.x) * 0.5f,
            segMin.y + (h - lsz.y) * 0.5f);
        draw->AddText(textPos, textCol, item);

        ImGui::PopID();
    }

    // Advance cursor past the strip.
    ImGui::SetCursorScreenPos(ImVec2(stripMin.x, stripMin.y + h));

    ImGui::PopID();
    return changed;
}

// ============================================================
// Section
// ============================================================
namespace {

// Per-label persistent open state for BeginSection. Keyed by the
// hashed label ID so multiple sections in the same window don't
// collide.
struct SectionState {
    bool open = true;
};

// Simple ID->state map, populated on first use. std::unordered_map
// via the C ABI would be overkill for the number of sections we
// typically render (< 50 in the whole app), so we use ImGui's own
// storage which the built-in CollapsingHeader also relies on.

}  // namespace

bool BeginSection(const char* label, bool defaultOpen) {
    ImGui::PushID(label);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID stateId = ImGui::GetID("##section_open");
    // If this is the first time we've seen this section, seed the
    // default.
    bool open = storage->GetBool(stateId, defaultOpen);

    const float h = T::SectionHeaderHeight;
    const float availWidth = ImGui::GetContentRegionAvail().x;
    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    const ImVec2 rowMax = ImVec2(rowMin.x + availWidth, rowMin.y + h);

    ImGui::InvisibleButton("##hdr", ImVec2(availWidth, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    const bool focused = ImGui::IsItemFocused();

    if (clicked) {
        open = !open;
        storage->SetBool(stateId, open);
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Subtle hover fill (Surface2 alpha 0.5) - the header is
    // structural chrome, not a button, so it shouldn't scream.
    if (hovered) {
        ImVec4 fill = ImVec4(T::Surface2.x, T::Surface2.y, T::Surface2.z, 0.50f);
        draw->AddRectFilled(rowMin, rowMax, U32(fill));
    }

    // Chevron - rotates 90 deg based on open state. Drawn to the
    // left of the label.
    const float chevPad = T::SpaceM;
    const ImVec2 chevCenter = ImVec2(rowMin.x + chevPad + 6.0f,
                                      rowMin.y + h * 0.5f);
    const float chevSize = 4.0f;
    ImVec2 c0, c1, c2;
    if (open) {
        // Points down.
        c0 = ImVec2(chevCenter.x - chevSize, chevCenter.y - chevSize * 0.5f);
        c1 = ImVec2(chevCenter.x + chevSize, chevCenter.y - chevSize * 0.5f);
        c2 = ImVec2(chevCenter.x,             chevCenter.y + chevSize * 0.75f);
    } else {
        // Points right.
        c0 = ImVec2(chevCenter.x - chevSize * 0.5f, chevCenter.y - chevSize);
        c1 = ImVec2(chevCenter.x - chevSize * 0.5f, chevCenter.y + chevSize);
        c2 = ImVec2(chevCenter.x + chevSize * 0.75f, chevCenter.y);
    }
    draw->AddTriangleFilled(c0, c1, c2, U32(T::TextSecondary));

    // Label uses the medium (semibold) font if loaded, or default.
    if (theme::gFontMedium) ImGui::PushFont(theme::gFontMedium);
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    const ImVec2 textPos = ImVec2(
        rowMin.x + chevPad + 6.0f + chevSize + 8.0f,
        rowMin.y + (h - labelSize.y) * 0.5f);
    draw->AddText(textPos, U32(T::TextPrimary), label);
    if (theme::gFontMedium) ImGui::PopFont();

    // 1px underline (BorderSubtle) across the whole row.
    draw->AddLine(
        ImVec2(rowMin.x, rowMax.y - 1.0f),
        ImVec2(rowMax.x, rowMax.y - 1.0f),
        U32(T::BorderSubtle),
        T::BorderThin);

    if (focused) {
        drawFocusOutline(draw, rowMin, rowMax, 0.0f);
    }

    // Add a bit of vertical breathing room before the body.
    if (open) {
        ImGui::Dummy(ImVec2(0.0f, T::SpaceXS));
        ImGui::Indent(chevPad);
    }

    return open;
}

void EndSection() {
    // Balance the Indent from BeginSection when the body was drawn.
    // ImGuiStorage lookup for symmetry: only unindent if the section
    // was open. Easier: always call Unindent and pair with the
    // Indent above - but BeginSection returns before Indent when
    // closed. Track with a small nested-open stack via PushID's
    // side effect? Simpler: peek at the current cursor's indent
    // level.
    //
    // Safer: track open state via the same storage key. We push a
    // scope ID so we can look it up here.
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID stateId = ImGui::GetID("##section_open");
    bool wasOpen = storage->GetBool(stateId, true);
    if (wasOpen) {
        ImGui::Unindent(T::SpaceM);
    }
    ImGui::Dummy(ImVec2(0.0f, T::SpaceS));
    ImGui::PopID();
}

// ============================================================
// TextNumber
// ============================================================
void TextNumber(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (theme::gFontMono) ImGui::PushFont(theme::gFontMono);
    ImGui::TextV(fmt, args);
    if (theme::gFontMono) ImGui::PopFont();
    va_end(args);
}

// ============================================================
// Spinner
// ============================================================
void Spinner(const char* label, float radius) {
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float ringDiameter = radius * 2.0f;
    const float labelWidth = label && label[0]
        ? ImGui::CalcTextSize(label).x
        : 0.0f;
    const float gap = label && label[0] ? T::SpaceS : 0.0f;
    const float totalWidth = ringDiameter + gap + labelWidth;
    const float lineHeight = std::max(ringDiameter, ImGui::GetTextLineHeight());

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Rotating arc. Advance ~1 full turn per second based on ImGui's
    // internal time so the spinner keeps ticking even when the parse
    // thread has the ActorMap locked. We render a 3/4 arc of the
    // reserved accent color as the "chasing" part, plus a faint full
    // ring underneath for visual anchor.
    const float t = static_cast<float>(ImGui::GetTime());
    constexpr int kSegments = 32;
    const ImVec2 center(cursor.x + radius, cursor.y + lineHeight * 0.5f);

    // Full faint ring.
    draw->AddCircle(center, radius,
                    U32(ImVec4(T::AccentSubtle.x, T::AccentSubtle.y,
                               T::AccentSubtle.z, 0.35f)),
                    kSegments, 2.0f);

    // Chasing arc.
    const float phase = t * 6.28318f;  // 2*PI rad/s = 1 full turn per second
    constexpr float kArcLen = 4.71238f;  // 3/4 turn
    draw->PathClear();
    for (int i = 0; i <= 24; ++i) {
        const float a = phase + (kArcLen * i / 24.0f);
        draw->PathLineTo(ImVec2(center.x + std::cos(a) * radius,
                                center.y + std::sin(a) * radius));
    }
    draw->PathStroke(U32(T::Accent), 0, 2.0f);

    if (label && label[0]) {
        const ImVec2 textPos(cursor.x + ringDiameter + gap,
                             center.y - ImGui::GetTextLineHeight() * 0.5f);
        draw->AddText(textPos, U32(T::TextSecondary), label);
    }

    ImGui::Dummy(ImVec2(totalWidth, lineHeight));
}

// ============================================================
// EmptyState
// ============================================================
bool EmptyState(const char* id,
                const char* glyph,
                const char* headline,
                const char* hint,
                const char* actionLabel) {
    ImGui::PushID(id);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImFont* font = ImGui::GetFont();
    const float glyphFontSize = ImGui::GetFontSize() * 2.4f;
    const ImVec2 glyphDim = font->CalcTextSizeA(glyphFontSize, 3.402823e38f, 0.0f, glyph);
    const ImVec2 headDim = ImGui::CalcTextSize(headline);
    const ImVec2 hintDim = hint ? ImGui::CalcTextSize(hint) : ImVec2(0, 0);

    float stackHeight = glyphDim.y + T::SpaceM + headDim.y;
    if (hint) stackHeight += T::SpaceS + hintDim.y;
    if (actionLabel) stackHeight += T::SpaceL + T::ControlHeight;

    // Sit the stack slightly above true center - reads better than
    // dead-center in tall panels, degrades to a small top pad in
    // short ones.
    const float topPad = std::max(T::SpaceL, (avail.y - stackHeight) * 0.4f);
    const float centerX = origin.x + avail.x * 0.5f;
    float y = origin.y + topPad;

    draw->AddText(font, glyphFontSize,
                  ImVec2(centerX - glyphDim.x * 0.5f, y),
                  U32(T::TextMuted), glyph);
    y += glyphDim.y + T::SpaceM;

    draw->AddText(ImVec2(centerX - headDim.x * 0.5f, y),
                  U32(T::TextSecondary), headline);
    y += headDim.y;

    if (hint) {
        y += T::SpaceS;
        draw->AddText(ImVec2(centerX - hintDim.x * 0.5f, y),
                      U32(T::TextMuted), hint);
        y += hintDim.y;
    }

    bool clicked = false;
    if (actionLabel) {
        y += T::SpaceL;
        const float buttonWidth = ImGui::CalcTextSize(actionLabel).x + 2.0f * T::ButtonPadX;
        ImGui::SetCursorScreenPos(ImVec2(centerX - buttonWidth * 0.5f, y));
        clicked = Button(actionLabel, ButtonVariant::Secondary);
        y += T::ControlHeight;
    }

    // Reserve the full block so following widgets land below it
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(avail.x, (y - origin.y) + T::SpaceL));

    ImGui::PopID();
    return clicked;
}

}  // namespace awlui
