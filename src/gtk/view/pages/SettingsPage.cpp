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

#include <algorithm>
#include <array>

namespace app::view {

namespace {
// Refresh interval spinner bounds (in milliseconds). 500ms feels snappy;
// 10s is the upper end before the UI looks frozen. Step 500 gives coarse
// control that's still precise enough for demo pacing.
constexpr int kRefreshIntervalMinMs  = 500;
constexpr int kRefreshIntervalMaxMs  = 10000;
constexpr int kRefreshIntervalStepMs = 500;

// Available palettes shown in the Settings thumbnail picker.
// NOTE: the `id` field must match:
//   - ThemeManager::setPalette() — drives which .css file loads
//   - .swatch-<id>-<0..3> CSS classes in sidebar.css — the color squares
// Adding a theme = append one entry here + 4 CSS classes + a themes/<id>.css
struct PaletteInfo {
    const char* id;
    const char* label;
};
constexpr std::array<PaletteInfo, 8> kPalettes = {{
    {"industrial", "Industrial"},
    {"nord",       "Nord"},
    {"dracula",    "Dracula"},
    {"crt",        "Retro CRT"},
    {"paper",      "Paper"},
    {"blueprint",  "Blueprint"},
    {"cockpit",    "Cockpit"},
    {"right",      "Right Sidebar"},
}};
// Palettes that ship a distinct main-window .ui (structural change).
// Must mirror chooseMainWindowUI() in MainWindow.cpp — used below to
// hide the "Show log panel" checkbox when logs move to a popover.
constexpr std::array<const char*, 1> kLayoutPalettesWithoutBottomLog = {
    "blueprint",
};
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
    paletteThumbs_    = builder->get_widget<Gtk::Box>("settings_palette_thumbs");
    radioFullscreen_  = builder->get_widget<Gtk::CheckButton>("settings_radio_fullscreen");

    buildPaletteThumbnails();
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

    // Highlight whichever thumbnail matches the saved palette id.
    // Empty config value maps to the "industrial" baseline card.
    const auto saved = config.getPalette();
    highlightSelectedPaletteCard(saved.empty() ? "industrial" : saved);

    // Layouts that route logs through a popover don't own a bottom
    // dock, so the "Show log panel at the bottom of the window"
    // checkbox is irrelevant there. Hide it for those.
    if (checkShowLogs_) {
        const bool hasBottomDock =
            std::find(kLayoutPalettesWithoutBottomLog.begin(),
                      kLayoutPalettesWithoutBottomLog.end(),
                      saved) == kLayoutPalettesWithoutBottomLog.end();
        checkShowLogs_->set_visible(hasBottomDock);
    }

    // Tier 2 palettes ship only one mode by design (Dracula, Retro
    // CRT, Blueprint, Cockpit are dark-only; Paper is light-only).
    // Disable the incompatible theme radio so the user can't end up
    // on a broken hybrid, and attach a tooltip that explains why.
    applyModeLockForPalette(saved);
}

void SettingsPage::applyModeLockForPalette(const std::string& paletteId) {
    const bool darkOnly =
        paletteId == "dracula"  || paletteId == "crt" ||
        paletteId == "blueprint"|| paletteId == "cockpit";
    const bool lightOnly = paletteId == "paper";

    if (radioDark_) {
        radioDark_->set_sensitive(!lightOnly);
        radioDark_->set_tooltip_text(
            lightOnly ? Glib::ustring(_("This palette is light-only by design."))
                      : Glib::ustring{});
    }
    if (radioLight_) {
        radioLight_->set_sensitive(!darkOnly);
        radioLight_->set_tooltip_text(
            darkOnly ? Glib::ustring(_("This palette is dark-only by design."))
                     : Glib::ustring{});
    }
}

void SettingsPage::buildPaletteThumbnails() {
    if (!paletteThumbs_) return;

    // Remove any previous children (refreshTranslations rebuilds ui).
    while (auto* child = paletteThumbs_->get_first_child()) {
        paletteThumbs_->remove(*child);
    }
    paletteCards_.clear();

    for (const auto& p : kPalettes) {
        auto* card = Gtk::make_managed<Gtk::Button>();
        card->add_css_class("palette-card");
        card->set_has_frame(false);

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);

        // 4-swatch color strip — one CSS class per (palette, slot).
        auto* swatches = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        swatches->add_css_class("palette-swatch-row");
        for (int i = 0; i < 4; ++i) {
            auto* sw = Gtk::make_managed<Gtk::Box>();
            sw->add_css_class("palette-swatch");
            sw->add_css_class(
                std::string("swatch-") + p.id + "-" + std::to_string(i));
            swatches->append(*sw);
        }
        content->append(*swatches);

        auto* name = Gtk::make_managed<Gtk::Label>(p.label);
        name->add_css_class("palette-card-name");
        name->set_xalign(0.5);
        content->append(*name);

        // Mode-support badge — makes it obvious at a glance that
        // some palettes ship only one mode by design. Lives as a
        // second (smaller, dimmed) label under the palette name so
        // the user doesn't have to discover the constraint by
        // clicking and watching a radio go disabled.
        //
        // N_() marks the literals for xgettext extraction without
        // translating at assignment time; the actual gettext call
        // happens in `_(badgeText)` below, which picks the right
        // catalog entry at render time. Using plain _() on the
        // assignments would work runtime-wise but leave xgettext
        // unable to see the strings because `badgeText` is a variable.
        const std::string pid{p.id};
        const char* badgeText = nullptr;
        if (pid == "paper")                                badgeText = N_("Light only");
        else if (pid == "dracula" || pid == "crt" ||
                 pid == "blueprint" || pid == "cockpit")   badgeText = N_("Dark only");
        else                                               badgeText = N_("Dark + Light");

        auto* badge = Gtk::make_managed<Gtk::Label>(_(badgeText));
        badge->add_css_class("palette-card-mode");
        badge->add_css_class("dim-label");
        badge->set_xalign(0.5);
        content->append(*badge);

        card->set_child(*content);
        const std::string id = p.id;
        card->signal_clicked().connect([this, id]() {
            if (syncingState_) return;
            auto& config = app::config::ConfigManager::instance();
            auto& logger = app::core::Application::instance().logger();

            // "industrial" card -> empty on disk (baseline, no extra CSS).
            const std::string toStore = (id == "industrial") ? "" : id;
            if (toStore == config.getPalette()) {
                // Still refresh highlight in case card is clicked twice
                highlightSelectedPaletteCard(id);
                return;
            }

            // Persist + highlight synchronously — config/card state
            // is cheap and visible changes are desirable here.
            if (!config.setPalette(toStore)) {
                logger.warn("Palette: failed to persist '{}' to config",
                            id.c_str());
            }
            logger.info("Palette: {}", id.c_str());
            highlightSelectedPaletteCard(id);
            applyModeLockForPalette(id);

            // DO NOT apply the CSS (ThemeManager::setPalette) here.
            // MainWindow handles that in a single signal_idle pass
            // alongside any structural relayout, so the user never
            // sees a "new CSS on old layout" hybrid frame.
            signalPaletteChanged_.emit(Glib::ustring(id));
        });

        paletteThumbs_->append(*card);
        paletteCards_[p.id] = card;
    }
}

void SettingsPage::highlightSelectedPaletteCard(const std::string& paletteId) {
    constexpr auto kSelected = "selected";
    for (auto& [id, card] : paletteCards_) {
        if (!card) continue;
        if (id == paletteId) {
            if (!card->has_css_class(kSelected)) card->add_css_class(kSelected);
        } else {
            card->remove_css_class(kSelected);
        }
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

// Handlers

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

void SettingsPage::onPaletteSelected() {
    // Legacy handler left for the header signature; the thumbnail
    // picker wires its selection inline in buildPaletteThumbnails().
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
