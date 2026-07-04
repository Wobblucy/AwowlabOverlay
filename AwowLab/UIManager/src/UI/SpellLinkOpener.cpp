#include "UI/SpellLinkOpener.h"

namespace ui {

namespace {
SpellLinkOpener& openerSlot() {
    static SpellLinkOpener opener;
    return opener;
}
} // anonymous namespace

void setSpellLinkOpener(SpellLinkOpener opener) {
    openerSlot() = std::move(opener);
}

const SpellLinkOpener& spellLinkOpener() {
    return openerSlot();
}

} // namespace ui
