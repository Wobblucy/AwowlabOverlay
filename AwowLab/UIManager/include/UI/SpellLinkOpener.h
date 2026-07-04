#pragma once
#include <cstdint>
#include <functional>

// Optional handler for opening a spell's page in the user's browser.
//
// The main app registers one at startup (see the Application constructor).
// Builds that stay offline, like the live overlay, register nothing - the
// context menu entry and Ctrl+click shortcut simply don't appear.
namespace ui {

using SpellLinkOpener = std::function<void(uint32_t spellId)>;

void setSpellLinkOpener(SpellLinkOpener opener);

// The registered opener, or an empty function when none was registered.
const SpellLinkOpener& spellLinkOpener();

} // namespace ui
