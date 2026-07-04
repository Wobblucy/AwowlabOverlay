#include "UI/AwlUI/Theme.h"

namespace awlui { namespace theme {

void CaptureFonts(ImFont* medium, ImFont* mono) {
    // Stored on the header-level inline globals so any translation
    // unit that includes Theme.h sees the same handles. Nullptr is
    // a valid handoff - widget PushFont sites treat nullptr as
    // "use the current default font."
    gFontMedium = medium;
    gFontMono   = mono;
}

}}  // namespace awlui::theme
