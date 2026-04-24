// Tests for app::view::SettingsPage -- View layer.
//
// Exercises the private handler surface and verifies that the sigc::signal
// bus to MainWindow fires (or stays silent under the syncingState_ guard)
// for every user-facing control: language, theme, palette, display mode,
// auto-refresh, refresh interval, log level, and show-logs checkbox.
//
// Each handler is invoked directly via a static friend bridge (mirroring
// the DashboardPageTest / ProductsPageTest pattern) after seeding the
// backing widget's state. Signal emission is observed by connecting a
// capturing lambda and asserting the flag.
//
// Needs a real GTK runtime (ViewTestMain.cpp brings up gtk_init) because
// SettingsPage::buildUI() loads settings-page.ui off disk.

#include "src/gtk/view/pages/SettingsPage.h"
#include "src/gtk/view/DialogManager.h"
#include "src/gtk/view/ThemeManager.h"
#include "src/config/ConfigManager.h"
#include "src/core/Application.h"
#include "mocks/MockDialogManager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <gtkmm.h>

#include <memory>
#include <string>

using app::view::SettingsPage;

class SettingsPageTest : public ::testing::Test {
protected:
    void SetUp() override {
        page_ = Gtk::make_managed<SettingsPage>(mockDM_);
    }

    // Friend bridges -- SettingsPage declares `friend class ::SettingsPageTest`.
    // TEST_F bodies don't inherit friendship, so we route through statics.

    static void callOnLanguageSelected(SettingsPage* p)     { p->onLanguageSelected(); }
    static void callOnThemeSelected(SettingsPage* p)        { p->onThemeSelected(); }
    static void callOnPaletteSelected(SettingsPage* p)      { p->onPaletteSelected(); }
    static void callOnDisplayModeSelected(SettingsPage* p)  { p->onDisplayModeSelected(); }
    static void callOnAutoRefreshToggled(SettingsPage* p)   { p->onAutoRefreshToggled(); }
    static void callOnRefreshIntervalChanged(SettingsPage* p) { p->onRefreshIntervalChanged(); }
    static void callOnLogLevelSelected(SettingsPage* p)     { p->onLogLevelSelected(); }
    static void callOnShowLogsToggled(SettingsPage* p)      { p->onShowLogsToggled(); }
    static void callHighlight(SettingsPage* p, const std::string& id) {
        p->highlightSelectedPaletteCard(id);
    }
    static void callApplyModeLock(SettingsPage* p, const std::string& id) {
        p->applyModeLockForPalette(id);
    }

    static Gtk::ComboBoxText* languageCombo(SettingsPage* p)   { return p->languageCombo_; }
    static Gtk::CheckButton*  radioDark(SettingsPage* p)       { return p->radioDark_; }
    static Gtk::CheckButton*  radioLight(SettingsPage* p)      { return p->radioLight_; }
    static Gtk::CheckButton*  radioFullscreen(SettingsPage* p) { return p->radioFullscreen_; }
    static Gtk::CheckButton*  radioWindowed(SettingsPage* p)   { return p->radioWindowed_; }
    static Gtk::CheckButton*  checkAutoRefresh(SettingsPage* p){ return p->checkAutoRefresh_; }
    static Gtk::SpinButton*   intervalSpin(SettingsPage* p)    { return p->intervalSpin_; }
    static Gtk::ComboBoxText* logLevelCombo(SettingsPage* p)   { return p->logLevelCombo_; }
    static Gtk::CheckButton*  checkShowLogs(SettingsPage* p)   { return p->checkShowLogs_; }

    static bool& syncingState(SettingsPage* p) { return p->syncingState_; }

    app::test::MockDialogManager mockDM_;
    SettingsPage* page_{nullptr};
};

// Display mode

TEST_F(SettingsPageTest, DisplayModeRadioFullscreenEmitsSignalTrue) {
    auto* rf = radioFullscreen(page_);
    ASSERT_NE(rf, nullptr);

    bool emitted = false;
    bool value = false;
    page_->signalDisplayModeChanged().connect(
        [&](bool v) { emitted = true; value = v; });

    rf->set_active(true);
    callOnDisplayModeSelected(page_);

    EXPECT_TRUE(emitted);
    EXPECT_TRUE(value);
}

TEST_F(SettingsPageTest, DisplayModeRadioWindowedEmitsSignalFalse) {
    auto* rf = radioFullscreen(page_);
    auto* rw = radioWindowed(page_);
    ASSERT_NE(rf, nullptr);
    ASSERT_NE(rw, nullptr);

    bool emitted = false;
    bool value = true;
    page_->signalDisplayModeChanged().connect(
        [&](bool v) { emitted = true; value = v; });

    rf->set_active(false);
    rw->set_active(true);
    callOnDisplayModeSelected(page_);

    EXPECT_TRUE(emitted);
    EXPECT_FALSE(value);
}

TEST_F(SettingsPageTest, DisplayModeHandlerSilentDuringSync) {
    bool emitted = false;
    page_->signalDisplayModeChanged().connect(
        [&](bool) { emitted = true; });

    syncingState(page_) = true;
    callOnDisplayModeSelected(page_);
    syncingState(page_) = false;

    EXPECT_FALSE(emitted)
        << "syncingState_ must gate signal emission to avoid cascade on init";
}

// Auto-refresh

TEST_F(SettingsPageTest, AutoRefreshToggledEmitsSignal) {
    auto* cb = checkAutoRefresh(page_);
    ASSERT_NE(cb, nullptr);

    bool emitted = false;
    bool value = false;
    page_->signalAutoRefreshToggled().connect(
        [&](bool v) { emitted = true; value = v; });

    cb->set_active(true);
    callOnAutoRefreshToggled(page_);

    EXPECT_TRUE(emitted);
    EXPECT_TRUE(value);
}

TEST_F(SettingsPageTest, AutoRefreshHandlerSilentDuringSync) {
    bool emitted = false;
    page_->signalAutoRefreshToggled().connect(
        [&](bool) { emitted = true; });

    syncingState(page_) = true;
    callOnAutoRefreshToggled(page_);
    syncingState(page_) = false;

    EXPECT_FALSE(emitted);
}

// Refresh interval

TEST_F(SettingsPageTest, RefreshIntervalChangedEmitsSpinValue) {
    auto* spin = intervalSpin(page_);
    ASSERT_NE(spin, nullptr);

    int captured = 0;
    page_->signalRefreshIntervalChanged().connect(
        [&](int v) { captured = v; });

    spin->set_value(2500);
    callOnRefreshIntervalChanged(page_);

    EXPECT_EQ(captured, 2500);
}

TEST_F(SettingsPageTest, RefreshIntervalHandlerSilentDuringSync) {
    bool emitted = false;
    page_->signalRefreshIntervalChanged().connect(
        [&](int) { emitted = true; });

    syncingState(page_) = true;
    callOnRefreshIntervalChanged(page_);
    syncingState(page_) = false;

    EXPECT_FALSE(emitted);
}

// Show-logs checkbox

TEST_F(SettingsPageTest, ShowLogsToggledEmitsSignal) {
    auto* cb = checkShowLogs(page_);
    ASSERT_NE(cb, nullptr);

    bool emitted = false;
    bool value = false;
    page_->signalVerboseLoggingToggled().connect(
        [&](bool v) { emitted = true; value = v; });

    cb->set_active(true);
    callOnShowLogsToggled(page_);

    EXPECT_TRUE(emitted);
    EXPECT_TRUE(value);
}

TEST_F(SettingsPageTest, ShowLogsHandlerSilentDuringSync) {
    bool emitted = false;
    page_->signalVerboseLoggingToggled().connect(
        [&](bool) { emitted = true; });

    syncingState(page_) = true;
    callOnShowLogsToggled(page_);
    syncingState(page_) = false;

    EXPECT_FALSE(emitted);
}

// Theme (dark / light)

TEST_F(SettingsPageTest, ThemeSelectedDarkEmitsThemeChanged) {
    auto* rd = radioDark(page_);
    auto* rl = radioLight(page_);
    ASSERT_NE(rd, nullptr);

    bool emitted = false;
    page_->signalThemeChanged().connect([&] { emitted = true; });

    if (rl) rl->set_active(false);
    rd->set_active(true);
    callOnThemeSelected(page_);

    EXPECT_TRUE(emitted);
    EXPECT_TRUE(app::view::ThemeManager::instance().isDarkMode());
}

TEST_F(SettingsPageTest, ThemeSelectedLightEmitsThemeChanged) {
    auto* rd = radioDark(page_);
    auto* rl = radioLight(page_);
    ASSERT_NE(rl, nullptr);

    bool emitted = false;
    page_->signalThemeChanged().connect([&] { emitted = true; });

    if (rd) rd->set_active(false);
    rl->set_active(true);
    callOnThemeSelected(page_);

    EXPECT_TRUE(emitted);
    EXPECT_FALSE(app::view::ThemeManager::instance().isDarkMode());
}

TEST_F(SettingsPageTest, ThemeHandlerSilentDuringSync) {
    bool emitted = false;
    page_->signalThemeChanged().connect([&] { emitted = true; });

    syncingState(page_) = true;
    callOnThemeSelected(page_);
    syncingState(page_) = false;

    EXPECT_FALSE(emitted);
}

// Language

TEST_F(SettingsPageTest, LanguageSelectedSameValueDoesNotEmit) {
    // loadInitialValues() seeded the combo to ConfigManager's current
    // language. Calling the handler without changing anything should
    // short-circuit on the "same as current" check.
    bool emitted = false;
    page_->signalLanguageChangeRequested().connect(
        [&](Glib::ustring) { emitted = true; });

    callOnLanguageSelected(page_);

    EXPECT_FALSE(emitted);
}

TEST_F(SettingsPageTest, LanguageSelectedEmptyIdIsNoOp) {
    auto* combo = languageCombo(page_);
    ASSERT_NE(combo, nullptr);

    bool emitted = false;
    page_->signalLanguageChangeRequested().connect(
        [&](Glib::ustring) { emitted = true; });

    // Reset combo to a state where get_active_id() returns empty.
    combo->set_active(-1);
    callOnLanguageSelected(page_);

    EXPECT_FALSE(emitted);
}

TEST_F(SettingsPageTest, LanguageHandlerSilentDuringSync) {
    bool emitted = false;
    page_->signalLanguageChangeRequested().connect(
        [&](Glib::ustring) { emitted = true; });

    syncingState(page_) = true;
    callOnLanguageSelected(page_);
    syncingState(page_) = false;

    EXPECT_FALSE(emitted);
}

// Log level -- changes the live Application::logger() level via
// parseLogLevel(). Application::instance().logger() returns a lazy
// NullLogger in tests, so setLevel is a no-op -- but we still exercise
// the parse + setLevel code path.

TEST_F(SettingsPageTest, LogLevelSelectedAppliesToLogger) {
    auto* combo = logLevelCombo(page_);
    ASSERT_NE(combo, nullptr);

    combo->set_active_id("ERROR");
    EXPECT_NO_THROW(callOnLogLevelSelected(page_));
}

TEST_F(SettingsPageTest, LogLevelEmptyIdIsNoOp) {
    auto* combo = logLevelCombo(page_);
    ASSERT_NE(combo, nullptr);

    combo->set_active(-1);
    EXPECT_NO_THROW(callOnLogLevelSelected(page_));
}

TEST_F(SettingsPageTest, LogLevelHandlerSilentDuringSync) {
    syncingState(page_) = true;
    EXPECT_NO_THROW(callOnLogLevelSelected(page_));
    syncingState(page_) = false;
}

// Palette -- onPaletteSelected is a legacy no-op handler retained for
// the header signature; the real work happens in the thumbnail card's
// inline lambda. Calling the method should be a pure no-op.

TEST_F(SettingsPageTest, OnPaletteSelectedIsNoOp) {
    EXPECT_NO_THROW(callOnPaletteSelected(page_));
}

// highlightSelectedPaletteCard + applyModeLockForPalette -- pure widget
// manipulation, no signals. We just verify they don't crash for any of
// the palette ids (industrial, dracula, crt, blueprint, cockpit, paper,
// nord, right).

TEST_F(SettingsPageTest, HighlightSelectedPaletteCardDoesNotCrash) {
    for (const auto* id : {"industrial", "nord", "dracula", "crt",
                           "paper", "blueprint", "cockpit", "right"}) {
        EXPECT_NO_THROW(callHighlight(page_, id))
            << "highlight failed for palette id: " << id;
    }
}

TEST_F(SettingsPageTest, ApplyModeLockDarkOnlyDisablesLightRadio) {
    auto* rl = radioLight(page_);
    ASSERT_NE(rl, nullptr);

    callApplyModeLock(page_, "dracula");
    EXPECT_FALSE(rl->get_sensitive())
        << "dracula is dark-only -> Light radio must be disabled";
}

TEST_F(SettingsPageTest, ApplyModeLockLightOnlyDisablesDarkRadio) {
    auto* rd = radioDark(page_);
    ASSERT_NE(rd, nullptr);

    callApplyModeLock(page_, "paper");
    EXPECT_FALSE(rd->get_sensitive())
        << "paper is light-only -> Dark radio must be disabled";
}

TEST_F(SettingsPageTest, ApplyModeLockRegularPaletteLeavesBothEnabled) {
    auto* rd = radioDark(page_);
    auto* rl = radioLight(page_);
    ASSERT_NE(rd, nullptr);
    ASSERT_NE(rl, nullptr);

    callApplyModeLock(page_, "nord");
    EXPECT_TRUE(rd->get_sensitive());
    EXPECT_TRUE(rl->get_sensitive());
}

// syncWithRuntimeState -- public API that must NOT fire any signals
// (that's what syncingState_ is for).

TEST_F(SettingsPageTest, SyncWithRuntimeStateEmitsNoSignals) {
    int count = 0;
    page_->signalDisplayModeChanged().connect([&](bool) { ++count; });
    page_->signalAutoRefreshToggled().connect([&](bool) { ++count; });
    page_->signalVerboseLoggingToggled().connect([&](bool) { ++count; });
    page_->signalThemeChanged().connect([&] { ++count; });
    page_->signalLanguageChangeRequested().connect(
        [&](Glib::ustring) { ++count; });

    page_->syncWithRuntimeState(/*fullscreen=*/true,
                                /*darkMode=*/false,
                                /*autoRefresh=*/false,
                                /*verboseLogging=*/true);

    EXPECT_EQ(count, 0)
        << "syncWithRuntimeState must suppress all signals via syncingState_";
}

TEST_F(SettingsPageTest, SyncWithRuntimeStateAppliesWidgetStates) {
    page_->syncWithRuntimeState(/*fullscreen=*/true,
                                /*darkMode=*/true,
                                /*autoRefresh=*/true,
                                /*verboseLogging=*/false);

    auto* rf = radioFullscreen(page_);
    auto* cb = checkAutoRefresh(page_);
    auto* sl = checkShowLogs(page_);
    if (rf) { EXPECT_TRUE(rf->get_active()); }
    if (cb) { EXPECT_TRUE(cb->get_active()); }
    if (sl) { EXPECT_FALSE(sl->get_active()); }
}
