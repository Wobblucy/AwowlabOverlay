#pragma once
#include <cstdint>
#include <imgui.h>

namespace awow {

// Interface for spell icon rendering
// Lets AnnotationManager and the shared meter/breakdown panels render
// spell icons without depending on the concrete SpellIconLoader (which
// lives here in UIManager). The standalone overlay passes nullptr and
// renders text-only rows.
class ISpellIconRenderer {
public:
    virtual ~ISpellIconRenderer() = default;

    // Render spell icon at the current ImGui cursor position
    // Returns true if icon was rendered (for SameLine() logic)
    virtual bool renderSpellIcon(uint32_t spellId, ImVec2 size) = 0;

    // Get texture ID for a spell (loads on-demand if not cached).
    // Returns 0 when no icon is available for the spell.
    virtual ImTextureID getSpellIcon(uint32_t spellId) = 0;
};

} // namespace awow
