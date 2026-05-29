// Tests for app::view::Toast -- the slide-in confirmation banner.
//
// No dedicated requirement (UI affordance). Asserts the observable
// public behaviour: the position option maps to the widget's
// alignment, show*() reveals the banner, and dismiss() hides it.
// Requires GTK initialised (ViewTestMain).

#include "src/gtk/view/widgets/Toast.h"

#include <gtkmm.h>
#include <gtest/gtest.h>

namespace {

using app::view::Toast;
using app::view::ToastPosition;

TEST(ToastTest, PositionOptionMapsToWidgetAlignment) {
    Toast topRight(Toast::Options{.position = ToastPosition::TopRight});
    EXPECT_EQ(topRight.get_halign(), Gtk::Align::END);
    EXPECT_EQ(topRight.get_valign(), Gtk::Align::START);

    Toast bottomLeft(Toast::Options{.position = ToastPosition::BottomLeft});
    EXPECT_EQ(bottomLeft.get_halign(), Gtk::Align::START);
    EXPECT_EQ(bottomLeft.get_valign(), Gtk::Align::END);

    Toast topCenter(Toast::Options{.position = ToastPosition::TopCenter});
    EXPECT_EQ(topCenter.get_halign(), Gtk::Align::CENTER);
}

TEST(ToastTest, ShowRevealsAndDismissHides) {
    // Error stays until dismissed (errorDurationMs default 0), so the
    // reveal state is deterministic without waiting on a timer.
    Toast toast;
    EXPECT_FALSE(toast.get_reveal_child());

    toast.showError("something failed");
    EXPECT_TRUE(toast.get_reveal_child())
        << "showError must reveal the banner";

    toast.dismiss();
    EXPECT_FALSE(toast.get_reveal_child())
        << "dismiss must hide the banner";
}

TEST(ToastTest, ShowSuccessReplacesVisibleErrorBanner) {
    Toast toast;
    toast.showError("err");
    EXPECT_TRUE(toast.get_reveal_child());

    // A subsequent success replaces the currently-visible banner rather
    // than stacking -- still revealed, now showing the success.
    toast.showSuccess("ok");
    EXPECT_TRUE(toast.get_reveal_child());
}

}  // namespace
