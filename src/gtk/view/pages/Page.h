#pragma once

#include <gtkmm.h>

namespace app::view {

class DialogManager;

/// Abstract base for every Notebook tab.
///
/// @design Factors out the three concerns every page shares:
///         - Layout: a vertical `Gtk::Box` root so pages can `append()` or
///           `set_child()` without worrying about the outer orientation.
///         - Dependencies: every page needs a `DialogManager&` for error
///           and confirmation popups, so the injection happens here.
///         - Lifecycle hooks: MainWindow dispatches the same three events
///           to every registered page uniformly.
///
/// @pattern Open/Closed — adding a new page means deriving from `Page` and
///          registering it with MainWindow; no page-specific wiring in the
///          main loop.
///
/// @thread_safety Hooks are called on the GTK main thread.
class Page : public Gtk::Box {
public:
    explicit Page(DialogManager& dialogManager)
        : Gtk::Box(Gtk::Orientation::VERTICAL)
        , dialogManager_(dialogManager) {}

    ~Page() override = default;

    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;
    Page(Page&&) = delete;
    Page& operator=(Page&&) = delete;

    /// Text shown on the Notebook tab. Must be localized via `_()` so the
    /// value reflects the currently active locale.
    [[nodiscard]] virtual Glib::ustring pageTitle() const = 0;

    /// Called after the user flips Dark/Light so pages with Cairo-drawn
    /// widgets can request a redraw. Default: no-op.
    virtual void onThemeChanged() {}

    /// Called before the page is torn down for a live language rebuild.
    /// Pages override this if they want to persist transient state (scroll
    /// position, selected row) across the rebuild. Default: no-op.
    virtual void onLanguageChanged() {}

protected:
    DialogManager& dialogManager_;
};

}  // namespace app::view
