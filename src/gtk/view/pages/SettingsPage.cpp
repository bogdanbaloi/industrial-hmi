// LoggerImpl brings parseLogLevel(); must come before any Windows header
// so the ERROR macro from wingdi.h doesn't clash with LogLevel::ERROR.
#include "src/core/LoggerImpl.h"

#include "SettingsPage.h"

#include "src/gtk/view/DialogManager.h"
#include "src/gtk/view/ThemeManager.h"
#include "src/config/ConfigManager.h"
#include "src/config/config_defaults.h"
#include "src/core/Application.h"
#include "src/core/i18n.h"

namespace app::view {

namespace {
// Refresh interval spinner bounds (in milliseconds). 500ms feels snappy;
// 10s is the upper end before the UI looks frozen. Step 500 gives coarse
// control that's still precise enough for demo pacing.
constexpr int kRefreshIntervalMinMs  = 500;
constexpr int kRefreshIntervalMaxMs  = 10000;
constexpr int kRefreshIntervalStepMs = 500;
}  // namespace

SettingsPage::SettingsPage(DialogManager& dialogManager)
    : Page(dialogManager) {
    buildUI();
    loadInitialValues();
    connectSignals();
}

Glib::ustring SettingsPage::pageTitle() const {
    return _("Settings");
}

void SettingsPage::buildUI() {
    auto builder = Gtk::Builder::create_from_file(
        app::config::defaults::kSettingsPageUI);

    auto* root = builder->get_widget<Gtk::ScrolledWindow>("settings_root");
    if (root) {
        append(*root);
    }

    languageCombo_    = builder->get_widget<Gtk::ComboBoxText>("settings_language_combo");
    radioDark_        = builder->get_widget<Gtk::CheckButton>("settings_radio_dark");
    radioLight_       = builder->get_widget<Gtk::CheckButton>("settings_radio_light");
    radioFullscreen_  = builder->get_widget<Gtk::CheckButton>("settings_radio_fullscreen");
    radioWindowed_    = builder->get_widget<Gtk::CheckButton>("settings_radio_windowed");
    checkAutoRefresh_ = builder->get_widget<Gtk::CheckButton>("settings_check_auto_refresh");
    intervalSpin_     = builder->get_widget<Gtk::SpinButton>("settings_interval_spin");
    logLevelCombo_    = builder->get_widget<Gtk::ComboBoxText>("settings_loglevel_combo");
    checkShowLogs_    = builder->get_widget<Gtk::CheckButton>("settings_check_show_logs");
}

void SettingsPage::loadInitialValues() {
    auto& config = app::config::ConfigManager::instance();

    if (languageCombo_) {
        languageCombo_->set_active_id(config.getLanguage());
    }

    if (intervalSpin_) {
        intervalSpin_->set_range(kRefreshIntervalMinMs, kRefreshIntervalMaxMs);
        intervalSpin_->set_increments(kRefreshIntervalStepMs,
                                      kRefreshIntervalStepMs * 2);
        intervalSpin_->set_value(app::config::defaults::kAutoRefreshIntervalMs);
    }

    if (logLevelCombo_) {
        logLevelCombo_->set_active_id(config.getLogLevel());
    }
}

void SettingsPage::connectSignals() {
    if (languageCombo_) {
        languageCombo_->signal_changed().connect(
            sigc::mem_fun(*this, &SettingsPage::onLanguageSelected));
    }
    if (radioDark_) {
        radioDark_->signal_toggled().connect(
            sigc::mem_fun(*this, &SettingsPage::onThemeSelected));
    }
    if (radioLight_) {
        radioLight_->signal_toggled().connect(
            sigc::mem_fun(*this, &SettingsPage::onThemeSelected));
    }
    if (radioFullscreen_) {
        radioFullscreen_->signal_toggled().connect(
            sigc::mem_fun(*this, &SettingsPage::onDisplayModeSelected));
    }
    if (checkAutoRefresh_) {
        checkAutoRefresh_->property_active().signal_changed().connect(
            sigc::mem_fun(*this, &SettingsPage::onAutoRefreshToggled));
    }
    if (intervalSpin_) {
        intervalSpin_->signal_value_changed().connect(
            sigc::mem_fun(*this, &SettingsPage::onRefreshIntervalChanged));
    }
    if (logLevelCombo_) {
        logLevelCombo_->signal_changed().connect(
            sigc::mem_fun(*this, &SettingsPage::onLogLevelSelected));
    }
    if (checkShowLogs_) {
        checkShowLogs_->property_active().signal_changed().connect(
            sigc::mem_fun(*this, &SettingsPage::onShowLogsToggled));
    }
}

void SettingsPage::syncWithRuntimeState(bool fullscreen,
                                        bool darkMode,
                                        bool autoRefresh,
                                        bool verboseLogging) {
    // set_active fires toggled/changed even when the value actually
    // differs, which would cascade into MainWindow's apply*() handlers
    // and re-run side effects (log panel show, auto-refresh timer). Guard
    // the block so handlers short-circuit while we're just syncing.
    syncingState_ = true;
    if (radioFullscreen_)  radioFullscreen_->set_active(fullscreen);
    if (radioWindowed_)    radioWindowed_->set_active(!fullscreen);
    if (radioDark_)        radioDark_->set_active(darkMode);
    if (radioLight_)       radioLight_->set_active(!darkMode);
    if (checkAutoRefresh_) checkAutoRefresh_->set_active(autoRefresh);
    if (checkShowLogs_)    checkShowLogs_->set_active(verboseLogging);
    syncingState_ = false;
}

// ----------------------------------------------------------------------------
// Handlers
// ----------------------------------------------------------------------------

void SettingsPage::onLanguageSelected() {
    if (syncingState_) return;
    if (!languageCombo_) return;

    const auto selectedId = languageCombo_->get_active_id();
    if (selectedId.empty()) return;

    auto& config = app::config::ConfigManager::instance();
    auto& logger = app::core::Application::instance().logger();

    if (std::string(selectedId) == config.getLanguage()) {
        return;  // Re-selected same value
    }

    if (!config.setLanguage(std::string(selectedId))) {
        logger.error("Failed to persist language preference");
        dialogManager_.showError(
            _("Error"),
            _("Could not save the language preference."),
            dynamic_cast<Gtk::Window*>(get_root()));
        return;
    }

    logger.info("Language preference saved: {}", std::string(selectedId));

    // Ask MainWindow to tear down + rebuild the pages so GtkBuilder
    // re-resolves every `translatable="yes"` string in the .ui files and
    // every live `_()` call picks up the new catalog. No restart needed.
    signalLanguageChangeRequested_.emit(selectedId);
}

void SettingsPage::onThemeSelected() {
    if (syncingState_) return;
    // Toggled fires on both sides; only act on activation.
    if (radioDark_ && radioDark_->get_active()) {
        ThemeManager::instance().setTheme(ThemeManager::Theme::DARK);
        app::core::Application::instance().logger().info("Theme: dark");
        signalThemeChanged_.emit();
    }
    if (radioLight_ && radioLight_->get_active()) {
        ThemeManager::instance().setTheme(ThemeManager::Theme::LIGHT);
        app::core::Application::instance().logger().info("Theme: light");
        signalThemeChanged_.emit();
    }
}

void SettingsPage::onDisplayModeSelected() {
    if (syncingState_) return;
    if (!radioFullscreen_) return;
    const bool wantFullscreen = radioFullscreen_->get_active();
    signalDisplayModeChanged_.emit(wantFullscreen);
}

void SettingsPage::onAutoRefreshToggled() {
    if (syncingState_) return;
    if (!checkAutoRefresh_) return;
    signalAutoRefreshToggled_.emit(checkAutoRefresh_->get_active());
}

void SettingsPage::onRefreshIntervalChanged() {
    if (syncingState_) return;
    if (!intervalSpin_) return;
    signalRefreshIntervalChanged_.emit(intervalSpin_->get_value_as_int());
}

void SettingsPage::onLogLevelSelected() {
    if (syncingState_) return;
    if (!logLevelCombo_) return;
    const auto levelId = std::string(logLevelCombo_->get_active_id());
    if (levelId.empty()) return;

    auto& app = app::core::Application::instance();
    app.logger().setLevel(app::core::parseLogLevel(levelId));
    app.logger().info("Log level set to: {}", levelId);
}

void SettingsPage::onShowLogsToggled() {
    if (syncingState_) return;
    if (!checkShowLogs_) return;
    signalVerboseLoggingToggled_.emit(checkShowLogs_->get_active());
}

}  // namespace app::view
