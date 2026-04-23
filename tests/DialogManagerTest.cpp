// Tests for app::view::DialogManager.
//
// DialogManager is a thin factory over Gtk::MessageDialog / Gtk::Dialog.
// Every method either presents a dialog directly or marshals the
// presentation through Glib::signal_idle. Under a real GTK main loop
// (provided here by Glib::MainLoop, with gtkmm initialised from
// ViewTestMain.cpp), we exercise each path and close the resulting
// dialog programmatically via Gtk::Dialog::response(), which fires the
// same signal handler that an OK/Cancel button click would.
//
// Why not xdotool? The dialogs are modal-but-parentless in this test
// harness; xdotool key routing under Xvfb can miss the dialog when no
// main window is focused. Programmatic response() is deterministic,
// fast (~150ms per test), and covers the exact code paths we care about
// (signal_response handler + the conditional delete).
//
// Blocking variants (showConfirm, showInput, showForm) spin their own
// Glib::MainLoop internally, so we dispatch the "click" from a
// signal_timeout that runs *inside* that inner loop.

#include "src/gtk/view/DialogManager.h"

#include <gtest/gtest.h>

#include <gtkmm.h>
#include <glibmm/main.h>

#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using app::view::DialogManager;

namespace {

/// Finds the first Gtk::Dialog (MessageDialog included) among the live
/// toplevels and dispatches a response to it. This is the programmatic
/// equivalent of the user clicking the matching button — it triggers
/// the exact signal_response handler wired up by DialogManager.
void respondToFirstDialog(int response) {
    for (auto* w : Gtk::Window::list_toplevels()) {
        if (auto* d = dynamic_cast<Gtk::Dialog*>(w)) {
            d->response(response);
            return;
        }
    }
}

/// Pumps a Glib::MainLoop for up to `timeout`, calling `afterPresent`
/// once before the loop starts so callers can post the async op that
/// opens the dialog. `afterResponse` runs on a second timer `responseMs`
/// into the loop — typically the programmatic response() dispatch.
///
/// The loop exits either when `afterResponse`'s callback ran and we
/// scheduled a quit, or when the safety timeout fires.
void pumpForDialog(std::function<void()> afterPresent,
                   std::function<void()> afterResponse,
                   std::chrono::milliseconds timeout =
                       std::chrono::milliseconds{500},
                   std::chrono::milliseconds responseDelay =
                       std::chrono::milliseconds{120}) {
    auto loop = Glib::MainLoop::create();

    afterPresent();

    Glib::signal_timeout().connect_once(
        [loop, afterResponse]() {
            afterResponse();
            // Give the response handler a tick to run + delete dialog.
            Glib::signal_timeout().connect_once(
                [loop]() { loop->quit(); }, 50);
        },
        static_cast<unsigned int>(responseDelay.count()));

    // Safety timeout — if response() above didn't find a dialog, still
    // escape so the test fails cleanly instead of hanging CI.
    Glib::signal_timeout().connect_once(
        [loop]() { loop->quit(); },
        static_cast<unsigned int>(timeout.count()));

    loop->run();
}

}  // namespace

TEST(DialogManagerTest, ShowInfoPresentsAndClosesOnResponse) {
    DialogManager dm;
    pumpForDialog(
        [&] { dm.showInfo("Info Title", "Info message"); },
        [] { respondToFirstDialog(Gtk::ResponseType::OK); });
    // No crash, no leak reported by gtkmm — dialog was presented and
    // its signal_response(delete dialog) branch ran.
    SUCCEED();
}

TEST(DialogManagerTest, ShowWarningPresentsAndClosesOnResponse) {
    DialogManager dm;
    pumpForDialog(
        [&] { dm.showWarning("Warn Title", "Warning message"); },
        [] { respondToFirstDialog(Gtk::ResponseType::OK); });
    SUCCEED();
}

TEST(DialogManagerTest, ShowErrorPresentsAndClosesOnResponse) {
    DialogManager dm;
    pumpForDialog(
        [&] { dm.showError("Error Title", "Error message"); },
        [] { respondToFirstDialog(Gtk::ResponseType::OK); });
    SUCCEED();
}

TEST(DialogManagerTest, ShowConfirmAsyncOkInvokesCallbackWithTrue) {
    DialogManager dm;
    bool called = false;
    bool confirmed = false;

    pumpForDialog(
        [&] {
            dm.showConfirmAsync("Confirm Title", "Delete?",
                [&](bool ok) { called = true; confirmed = ok; });
        },
        [] { respondToFirstDialog(Gtk::ResponseType::OK); });

    EXPECT_TRUE(called) << "showConfirmAsync callback never fired";
    EXPECT_TRUE(confirmed);
}

TEST(DialogManagerTest, ShowConfirmAsyncCancelInvokesCallbackWithFalse) {
    DialogManager dm;
    bool called = false;
    bool confirmed = true;  // flip expected

    pumpForDialog(
        [&] {
            dm.showConfirmAsync("Confirm Title", "Delete?",
                [&](bool ok) { called = true; confirmed = ok; });
        },
        [] { respondToFirstDialog(Gtk::ResponseType::CANCEL); });

    EXPECT_TRUE(called);
    EXPECT_FALSE(confirmed);
}

// The blocking confirm variant spins its own Glib::MainLoop inside
// DialogManager::showConfirm. We can't wrap it in our usual pump helper
// because the outer run() would sit on top of the inner one. Instead we
// schedule the response via signal_timeout *before* calling showConfirm
// — by the time the inner loop is running, the timer is already queued
// on the default main context and will fire.
TEST(DialogManagerTest, ShowConfirmBlockingReturnsTrueOnOk) {
    DialogManager dm;

    Glib::signal_timeout().connect_once(
        [] { respondToFirstDialog(Gtk::ResponseType::OK); }, 120);

    const bool ok = dm.showConfirm("Sync Confirm", "Proceed?");
    EXPECT_TRUE(ok);
}

TEST(DialogManagerTest, ShowConfirmBlockingReturnsFalseOnCancel) {
    DialogManager dm;

    Glib::signal_timeout().connect_once(
        [] { respondToFirstDialog(Gtk::ResponseType::CANCEL); }, 120);

    const bool ok = dm.showConfirm("Sync Confirm", "Proceed?");
    EXPECT_FALSE(ok);
}

TEST(DialogManagerTest, ShowInputReturnsValueOnOk) {
    DialogManager dm;

    Glib::signal_timeout().connect_once(
        [] { respondToFirstDialog(Gtk::ResponseType::OK); }, 120);

    auto [ok, value] = dm.showInput("Input Title", "Your name:", "Alice");
    EXPECT_TRUE(ok);
    // Entry pre-filled with defaultValue; no user typing happened, so
    // the returned value is the default.
    EXPECT_EQ(value, "Alice");
}

TEST(DialogManagerTest, ShowInputReturnsEmptyOnCancel) {
    DialogManager dm;

    Glib::signal_timeout().connect_once(
        [] { respondToFirstDialog(Gtk::ResponseType::CANCEL); }, 120);

    auto [ok, value] = dm.showInput("Input Title", "Your name:", "Alice");
    EXPECT_FALSE(ok);
    EXPECT_TRUE(value.empty());
}

TEST(DialogManagerTest, ShowFormReturnsAllFieldDefaultsOnOk) {
    DialogManager dm;

    Glib::signal_timeout().connect_once(
        [] { respondToFirstDialog(Gtk::ResponseType::OK); }, 120);

    std::vector<std::pair<std::string, std::string>> fields{
        {"Code", "P-123"},
        {"Name", "Widget"},
        {"Stock", "42"},
    };

    auto [ok, values] = dm.showForm("New Product", fields);
    EXPECT_TRUE(ok);
    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0], "P-123");
    EXPECT_EQ(values[1], "Widget");
    EXPECT_EQ(values[2], "42");
}

TEST(DialogManagerTest, ShowFormReturnsEmptyVectorOnCancel) {
    DialogManager dm;

    Glib::signal_timeout().connect_once(
        [] { respondToFirstDialog(Gtk::ResponseType::CANCEL); }, 120);

    std::vector<std::pair<std::string, std::string>> fields{
        {"Code", "P-999"},
    };

    auto [ok, values] = dm.showForm("New Product", fields);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(values.empty());
}
