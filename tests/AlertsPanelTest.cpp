// [utest->req~alarm-005~1]
// Covers REQ-ALARM-005 (operator-visible Shelved inventory, Phase 4b).
//
// Tests for app::view::AlertsPanel
//
// AlertsPanel is a GTK widget (heavyweight to instantiate in a unit
// test: needs a Gtk::Application context + Display). Phase 4b's signal
// is concentrated in one pure helper -- `formatCountdown` -- which
// converts a `std::chrono::seconds` into the operator-facing
// "M:SS left" / "EXPIRED" string the Shelved subsection renders.
//
// That helper has no GTK dependency beyond `Glib::ustring` for the
// return type (already linked because every test pulls GTKMM headers
// for sigc++), so we can pin its contract here without standing up the
// full widget tree. The full rendering path is covered by manual
// smoke + the existing AlertCenterTest cases on `shelvedSnapshot()`.

#include "src/gtk/view/widgets/AlertsPanel.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

using app::view::AlertsPanel;

TEST(AlertsPanelFormatCountdown, ZeroRendersExpired) {
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{0}), "EXPIRED");
}

TEST(AlertsPanelFormatCountdown, NegativeRendersExpired) {
    // The model clamps `secondsRemaining` at 0 for entries past their
    // deadline, but the formatter accepts negative durations defensively
    // so a future change in clamping behaviour does not surprise the UI.
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{-5}),
              "EXPIRED");
}

TEST(AlertsPanelFormatCountdown, UnderOneMinutePadsSeconds) {
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{1}),
              "0:01 left");
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{9}),
              "0:09 left");
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{30}),
              "0:30 left");
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{59}),
              "0:59 left");
}

TEST(AlertsPanelFormatCountdown, RolloverToMinutes) {
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{60}),
              "1:00 left");
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{61}),
              "1:01 left");
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{119}),
              "1:59 left");
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{120}),
              "2:00 left");
}

TEST(AlertsPanelFormatCountdown, DefaultFiveMinuteShelve) {
    // The default shelf duration on the Shelve button is 5 min; the
    // moment-after-shelve countdown is the most-visible-in-the-wild
    // formatter input.
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{300}),
              "5:00 left");
}

TEST(AlertsPanelFormatCountdown, LongShelveStaysMmSs) {
    // ISA-18.2 shelve durations are usually minutes; the formatter
    // intentionally keeps "M:SS" past 60 min instead of switching to
    // "HH:MM:SS" because the operator-canonical scale is minutes.
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{3600}),
              "60:00 left");
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{3930}),
              "65:30 left");
}

TEST(AlertsPanelFormatCountdown, SecondsPadOnlyUnderTen) {
    // The pad rule is "secs < 10 -> '0X', else 'XX'". A 10s remainder
    // must NOT render as "1:010 left".
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{70}),
              "1:10 left");
    EXPECT_EQ(AlertsPanel::formatCountdown(std::chrono::seconds{99}),
              "1:39 left");
}
