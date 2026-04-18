#ifndef SETTINGS_PAGE_H
#define SETTINGS_PAGE_H

#include "src/gtk/view/pages/Page.h"
#include <gtkmm.h>
#include <sigc++/signal.h>

namespace app::view {

/// Settings page - dedicated Notebook tab that centralizes user-facing
/// preferences (language, theme, display mode, auto-refresh, logging).
///
/// Design:
/// - Pure View: reads/writes ConfigManager + calls ThemeManager/Logger
///   directly for concerns fully owned by those singletons.
/// - Emits sigc signals for concerns owned by MainWindow (display mode,
///   auto-refresh timer, log panel visibility, themed-widget redraw,
///   language rebuild).
/// - Language persists to disk (ConfigManager::setLanguage). Theme,
///   auto-refresh state, interval, and log level are session-only: they
///   take effect immediately but are not written back to app-config.json.
class SettingsPage : public Page {
public:
    explicit SettingsPage(DialogManager& dialogManager);
    ~SettingsPage() override = default;

    // Page overrides
    [[nodiscard]] Glib::ustring pageTitle() const override;

    /// Sync the Settings widgets with the actual runtime state. Call after
    /// MainWindow finishes initializing so the radios/checkboxes reflect
    /// whatever was set programmatically (e.g., start-in-fullscreen).
    void syncWithRuntimeState(bool fullscreen,
                              bool darkMode,
                              bool autoRefresh,
                              bool verboseLogging);

    // --- Signals consumed by MainWindow -------------------------------------
    // Emitted when the user flips the Fullscreen/Windowed radio.
    sigc::signal<void(bool)>& signalDisplayModeChanged() {
        return signalDisplayModeChanged_;
    }
    // Emitted after the theme has been applied (for Cairo widget redraws).
    sigc::signal<void()>& signalThemeChanged() {
        return signalThemeChanged_;
    }
    // Emitted when the Auto Refresh checkbox toggles.
    sigc::signal<void(bool)>& signalAutoRefreshToggled() {
        return signalAutoRefreshToggled_;
    }
    // Emitted when the refresh interval spinner changes.
    sigc::signal<void(int)>& signalRefreshIntervalChanged() {
        return signalRefreshIntervalChanged_;
    }
    // Emitted when the Show Log Panel checkbox toggles.
    sigc::signal<void(bool)>& signalVerboseLoggingToggled() {
        return signalVerboseLoggingToggled_;
    }
    // Emitted when the user picks a different language. MainWindow tears
    // down and rebuilds the pages so new translations apply live.
    sigc::signal<void(Glib::ustring)>& signalLanguageChangeRequested() {
        return signalLanguageChangeRequested_;
    }

private:
    void buildUI();
    void connectSignals();
    void loadInitialValues();

    // Handlers (wired to widget signals; distinct from the Page virtual
    // hooks of similar names — note the `Selected` / `Toggled` suffixes).
    void onLanguageSelected();
    void onThemeSelected();
    void onDisplayModeSelected();
    void onAutoRefreshToggled();
    void onRefreshIntervalChanged();
    void onLogLevelSelected();
    void onShowLogsToggled();

    // Guard: set while `syncWithRuntimeState` is writing back to the
    // widgets programmatically, so the side-effect toggled/changed signals
    // don't fire application-level handlers (would cause spurious
    // double-apply — e.g. "Verbose logging enabled" twice on rebuild).
    bool syncingState_ = false;

    // Widgets
    Gtk::ComboBoxText* languageCombo_ = nullptr;
    Gtk::CheckButton*  radioDark_ = nullptr;
    Gtk::CheckButton*  radioLight_ = nullptr;
    Gtk::CheckButton*  radioFullscreen_ = nullptr;
    Gtk::CheckButton*  radioWindowed_ = nullptr;
    Gtk::CheckButton*  checkAutoRefresh_ = nullptr;
    Gtk::SpinButton*   intervalSpin_ = nullptr;
    Gtk::ComboBoxText* logLevelCombo_ = nullptr;
    Gtk::CheckButton*  checkShowLogs_ = nullptr;

    // Signals
    sigc::signal<void(bool)> signalDisplayModeChanged_;
    sigc::signal<void()>     signalThemeChanged_;
    sigc::signal<void(bool)> signalAutoRefreshToggled_;
    sigc::signal<void(int)>  signalRefreshIntervalChanged_;
    sigc::signal<void(bool)>           signalVerboseLoggingToggled_;
    sigc::signal<void(Glib::ustring)>  signalLanguageChangeRequested_;
};

}  // namespace app::view

#endif  // SETTINGS_PAGE_H
