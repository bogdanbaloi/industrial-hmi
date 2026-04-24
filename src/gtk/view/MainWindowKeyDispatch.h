#pragma once

// Keyboard-shortcut dispatcher for MainWindow.
//
// Lives separately from MainWindow.cpp so the switch/if-chain for
// F1..F11/Esc can be unit-tested without instantiating MainWindow
// (which pulls in Notebook, Presenters, DatabaseManager, timers,
// ThemeManager, etc.).
//
// MainWindow::onKeyPressed fills in a KeyDispatchContext with the
// callbacks that actually touch MainWindow state, then forwards to
// dispatchKey(). Tests construct their own context with capturing
// lambdas and assert that the right callback fired for the right
// keyval.
//
// The API deliberately takes `unsigned int` for the keyval instead of
// `guint` so the header doesn't need to pull <glib.h> or gtkmm.h --
// tests can include it as a pure C++ header.

#include <functional>

namespace app::view {

/// Callback bundle for MainWindow's keyboard shortcuts.
///
/// Every callback is optional -- a null std::function is treated as
/// "nothing to do, but the key was handled" (returns true from the
/// dispatcher). That mirrors MainWindow's previous inline behaviour
/// where e.g. F2 returned true even if `mainNotebook_` was still null.
///
/// Page switching is expressed via `pageCount` + `onPageSwitch(index)`
/// rather than a `Gtk::Notebook*` so this header stays GTK-free.
struct KeyDispatchContext {
    /// Number of pages currently registered. Used to bounds-check
    /// F2 (index 0) / F3 (index 1) / F4 (index 2).
    int pageCount = 0;

    /// Called with the target index when F2/F3/F4 resolves to a
    /// valid page. Not called when the index is out of range.
    std::function<void(int)> onPageSwitch;

    /// Called when the user pressed F5 (manual refresh tick).
    std::function<void()> onRefresh;

    /// Called when the user pressed F1 (About dialog).
    std::function<void()> onAbout;

    /// Called when the user pressed F11 (toggle fullscreen).
    std::function<void()> onFullscreenToggle;

    /// True when the window is currently fullscreen. Gates the Esc
    /// handler -- Esc is a no-op outside fullscreen so it doesn't
    /// swallow the key from child widgets (e.g. dialog cancel).
    bool isFullscreen = false;

    /// Called when Esc is pressed while `isFullscreen == true`.
    std::function<void()> onExitFullscreen;
};

/// Dispatches a GTK keyval against the context and invokes at most one
/// callback. Returns true if the key was recognised (so the GTK event
/// controller reports it handled and stops propagation), false if the
/// dispatcher has no opinion.
///
/// `keyval` is typed `unsigned int` to keep this header GTK-free; in
/// production it receives the `guint` from GTK's event controller.
bool dispatchKey(unsigned int keyval, const KeyDispatchContext& ctx);

}  // namespace app::view
