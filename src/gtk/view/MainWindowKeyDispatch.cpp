#include "src/gtk/view/MainWindowKeyDispatch.h"

#include <gdk/gdk.h>  // GDK_KEY_* keysym macros

namespace app::view {

bool dispatchKey(unsigned int keyval, const KeyDispatchContext& ctx) {
    // F1 — About dialog.
    if (keyval == GDK_KEY_F1) {
        if (ctx.onAbout) ctx.onAbout();
        return true;
    }

    // F2 / F3 / F4 — jump to the 1st/2nd/3rd registered page.
    // Out-of-range indices return false (key unhandled) so the event
    // can propagate; in practice this only happens before pages are
    // registered, in which case the user wouldn't see the window yet.
    auto tryPageSwitch = [&](int index) -> bool {
        if (index < 0 || index >= ctx.pageCount) return false;
        if (ctx.onPageSwitch) ctx.onPageSwitch(index);
        return true;
    };
    if (keyval == GDK_KEY_F2) return tryPageSwitch(0);
    if (keyval == GDK_KEY_F3) return tryPageSwitch(1);
    if (keyval == GDK_KEY_F4) return tryPageSwitch(2);

    // F5 — manual refresh (advance the simulation one tick).
    if (keyval == GDK_KEY_F5) {
        if (ctx.onRefresh) ctx.onRefresh();
        return true;
    }

    // F11 — toggle fullscreen.
    if (keyval == GDK_KEY_F11) {
        if (ctx.onFullscreenToggle) ctx.onFullscreenToggle();
        return true;
    }

    // Esc — exit fullscreen if we're in it. Outside fullscreen we stay
    // silent so child widgets (dialog close, search cancel, …) keep
    // their Esc binding.
    if (keyval == GDK_KEY_Escape && ctx.isFullscreen) {
        if (ctx.onExitFullscreen) ctx.onExitFullscreen();
        return true;
    }

    return false;
}

}  // namespace app::view
