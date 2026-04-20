#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <gtkmm.h>
#include <memory>
#include <vector>
#include <filesystem>
#include "src/core/LoggerBase.h"

// Forward declarations
namespace app {
    class DashboardPresenter;
    class ProductsPresenter;

    namespace presenter {
        class AlertCenter;
    }

    namespace view {
        class Page;
        class DashboardPage;
        class ProductsPage;
        class SettingsPage;
        class DialogManager;
        class AlertsPanel;
        class SystemStatusBadge;
        class LiveClock;
    }

    namespace core {
        class Logger;
        class ExceptionHandler;
    }
}

/// Main application window.
///
/// Responsibilities:
/// - Load the window layout from main-window.ui
/// - Own the pages registry (`pages_`) and dispatch lifecycle hooks
///   uniformly across Dashboard / Products / Settings
/// - Own the auto-refresh timer + log-panel tail (driven by SettingsPage)
/// - Handle global keyboard shortcuts (F1-F5, F11, Esc)
/// - Tear down + rebuild pages when the user picks a new language, so
///   every `_()` call and every `translatable="yes"` property in the .ui
///   files resolves against the new catalog without an app restart.
class MainWindow : public Gtk::ApplicationWindow {
public:
    MainWindow();
    ~MainWindow() override;

private:
    // UI initialization
    void loadUI();
    // Parse the palette-appropriate main-window .ui and assign the
    // member widget pointers (mainNotebook_, alertsContainer_, etc.)
    // to the freshly-built subtree WITHOUT installing it via
    // set_child(). Caller is responsible for calling set_child()
    // once the new subtree is fully populated — used by reloadLayout
    // to avoid showing an empty frame between swap and repopulate.
    Gtk::Box* parseLayoutUI();
    void loadSidebarCSS();
    void setupKeyboardShortcuts();
    void createAllPages();
    void wireSettingsSignals();
    void registerPage(app::view::Page* page);
    void clearPages();
    void rebuildPages(const Glib::ustring& newLanguage);

    // Pick which main-window .ui to parse for the currently-active
    // palette. Structural palettes (e.g. Blueprint with top-bar
    // instead of sidebar) return their own path; everything else
    // returns the default sidebar layout.
    [[nodiscard]] static const char* chooseMainWindowUI(
        const std::string& palette);
    // Build the sidebar/top-bar inhabitants (AlertsPanel, SystemStatusBadge,
    // LiveClock) and hook up E-STOP + Close handlers. Safe to call
    // multiple times during a live relayout — callers are expected to
    // have reset the member pointers to null first.
    void buildSidebarWidgets();
    // Tear down + re-parse main-window.ui from disk when switching
    // between structurally different layouts (e.g. default → Blueprint
    // top-bar). Keeps the user's active tab and runtime toggles.
    void reloadLayout();

    // Timer / panel helpers (driven by SettingsPage signals)
    void applyDisplayMode(bool fullscreen);
    void applyAutoRefresh(bool enabled);
    void applyRefreshInterval(int intervalMs);
    void applyVerboseLogging(bool enabled);
    void onThemeApplied();

    bool onKeyPressed(guint keyval, guint keycode, Gdk::ModifierType state);
    void toggleFullscreen();

    // Runtime state
    bool isFullscreen_    = false;
    bool autoRefreshOn_   = true;
    int  refreshIntervalMs_;
    bool verboseLogging_  = false;
    // Path of the main-window .ui currently loaded; used to detect
    // whether a palette change requires a hot relayout.
    std::string currentLayoutUI_;
    // Held across parseLayoutUI() + (populate + set_child) so the
    // builder-owned widgets don't drop their initial ref before a
    // parent adopts them. Reset immediately after set_child().
    Glib::RefPtr<Gtk::Builder> pendingBuilder_;

    // Widgets from .ui file
    sigc::connection autoRefreshTimer_;
    Gtk::Box*        logPanel_        = nullptr;
    Gtk::TextView*   logTextView_     = nullptr;
    sigc::connection logRefreshConnection_;
    std::size_t      lastLogSize_{0};
    Gtk::Notebook*   mainNotebook_          = nullptr;
    Gtk::Box*        alertsContainer_       = nullptr;
    Gtk::Box*        systemStatusContainer_ = nullptr;
    Gtk::Box*        clockContainer_        = nullptr;
    Gtk::Button*     estopButton_           = nullptr;
    // Only present in Blueprint layout — hosts log_panel in a popover
    // instead of the bottom dock. When the widget exists we wire the
    // log tail timer to auto-start so the popover is never empty.
    Gtk::MenuButton* logButton_             = nullptr;

    // Sidebar labels/buttons that carry translatable text. Kept as
    // members so `rebuildPages()` can reassign the text via `_()` after
    // a live language switch — the .ui file is only loaded once at
    // startup, so GtkBuilder won't re-translate these by itself.
    Gtk::Label*      appTitleLabel_    = nullptr;
    Gtk::Label*      appSubtitleLabel_ = nullptr;
    Gtk::Button*     closeAppButton_   = nullptr;
    Gtk::Label*      versionLabel_     = nullptr;
    Gtk::Label*      authorLabel_      = nullptr;
    void refreshSidebarTranslations();

    // Services (injected into pages)
    std::unique_ptr<app::core::ExceptionHandler> exceptionHandler_;
    std::unique_ptr<app::view::DialogManager>    dialogManager_;

    // Page registry. Pages are Gtk::make_managed, so removing them from
    // the Notebook destroys them; we keep raw pointers only for dispatch.
    std::vector<app::view::Page*> pages_;

    // Typed handles for places that need page-specific API
    // (observer wiring, settings-signal connections, themed-widget redraw).
    std::shared_ptr<app::DashboardPresenter> dashboardPresenter_;
    app::view::DashboardPage*                dashboardPage_ = nullptr;

    std::shared_ptr<app::ProductsPresenter>  productsPresenter_;
    app::view::ProductsPage*                 productsPage_  = nullptr;

    app::view::SettingsPage*                 settingsPage_  = nullptr;

    // Sidebar Alert center + view — owned by the window, shared with
    // DashboardPresenter which raises/clears alerts on state transitions.
    std::unique_ptr<app::presenter::AlertCenter> alertCenter_;
    app::view::AlertsPanel*                      alertsPanel_ = nullptr;
    app::view::SystemStatusBadge*                statusBadge_ = nullptr;
    app::view::LiveClock*                        clock_       = nullptr;
};

#endif  // MAIN_WINDOW_H
