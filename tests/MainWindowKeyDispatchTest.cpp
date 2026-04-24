// Tests for the MainWindow keyboard-shortcut dispatcher.
//
// MainWindow::onKeyPressed was refactored to delegate to the free
// function `dispatchKey(keyval, ctx)`. The dispatcher is pure -- it
// owns no Gtk widgets, no singletons, no MainWindow state -- so these
// tests capture callback invocations via std::function flags and
// assert the mapping keyval -> callback directly.
//
// GDK keyval constants are spelled with their numeric values here so
// the test TU doesn't have to pull <gdk/gdkkeysyms.h> into its
// include path. The values are fixed X11 keysyms and haven't changed
// in decades.

#include "src/gtk/view/MainWindowKeyDispatch.h"

#include <gtest/gtest.h>

#include <functional>

namespace {

// X11 keysyms -- stable ABI, mirrored from /usr/include/X11/keysymdef.h
// (and in gdk/gdkkeysyms.h). Duplicated here to keep the test GTK-free.
constexpr unsigned int kKeyF1     = 0xffbe;
constexpr unsigned int kKeyF2     = 0xffbf;
constexpr unsigned int kKeyF3     = 0xffc0;
constexpr unsigned int kKeyF4     = 0xffc1;
constexpr unsigned int kKeyF5     = 0xffc2;
constexpr unsigned int kKeyF11    = 0xffc8;
constexpr unsigned int kKeyEscape = 0xff1b;
constexpr unsigned int kKeyA      = 0x0061;  // random unhandled key

using app::view::KeyDispatchContext;
using app::view::dispatchKey;

/// Builds a context where every callback sets a boolean flag so the
/// tests can assert which one fired.
struct CallbackFlags {
    bool about = false;
    bool refresh = false;
    bool fullscreenToggle = false;
    bool exitFullscreen = false;
    int  switchedToPage = -1;
};

KeyDispatchContext makeContext(CallbackFlags& flags,
                               int pageCount = 3,
                               bool isFullscreen = false) {
    KeyDispatchContext ctx;
    ctx.pageCount = pageCount;
    ctx.isFullscreen = isFullscreen;
    ctx.onPageSwitch = [&](int index) { flags.switchedToPage = index; };
    ctx.onAbout = [&] { flags.about = true; };
    ctx.onRefresh = [&] { flags.refresh = true; };
    ctx.onFullscreenToggle = [&] { flags.fullscreenToggle = true; };
    ctx.onExitFullscreen = [&] { flags.exitFullscreen = true; };
    return ctx;
}

}  // namespace

// F1 -- About dialog

TEST(MainWindowKeyDispatchTest, F1InvokesOnAbout) {
    CallbackFlags flags;
    auto ctx = makeContext(flags);

    EXPECT_TRUE(dispatchKey(kKeyF1, ctx));
    EXPECT_TRUE(flags.about);
    EXPECT_FALSE(flags.refresh);
    EXPECT_FALSE(flags.fullscreenToggle);
    EXPECT_EQ(flags.switchedToPage, -1);
}

TEST(MainWindowKeyDispatchTest, F1ReturnsTrueEvenWithNullCallback) {
    KeyDispatchContext ctx;
    // onAbout left default-constructed (empty).
    EXPECT_TRUE(dispatchKey(kKeyF1, ctx));
}

// F2 / F3 / F4 -- page switch

TEST(MainWindowKeyDispatchTest, F2SwitchesToPageZero) {
    CallbackFlags flags;
    auto ctx = makeContext(flags);

    EXPECT_TRUE(dispatchKey(kKeyF2, ctx));
    EXPECT_EQ(flags.switchedToPage, 0);
}

TEST(MainWindowKeyDispatchTest, F3SwitchesToPageOne) {
    CallbackFlags flags;
    auto ctx = makeContext(flags);

    EXPECT_TRUE(dispatchKey(kKeyF3, ctx));
    EXPECT_EQ(flags.switchedToPage, 1);
}

TEST(MainWindowKeyDispatchTest, F4SwitchesToPageTwo) {
    CallbackFlags flags;
    auto ctx = makeContext(flags);

    EXPECT_TRUE(dispatchKey(kKeyF4, ctx));
    EXPECT_EQ(flags.switchedToPage, 2);
}

TEST(MainWindowKeyDispatchTest, F4ReturnsFalseWhenOutOfRange) {
    CallbackFlags flags;
    // Only 2 pages registered -- F4 (index 2) is out of range.
    auto ctx = makeContext(flags, /*pageCount=*/2);

    EXPECT_FALSE(dispatchKey(kKeyF4, ctx));
    EXPECT_EQ(flags.switchedToPage, -1)
        << "page switch callback must not fire on out-of-range index";
}

TEST(MainWindowKeyDispatchTest, F2ReturnsFalseWhenNoPages) {
    CallbackFlags flags;
    auto ctx = makeContext(flags, /*pageCount=*/0);

    EXPECT_FALSE(dispatchKey(kKeyF2, ctx));
    EXPECT_EQ(flags.switchedToPage, -1);
}

// F5 -- refresh

TEST(MainWindowKeyDispatchTest, F5InvokesOnRefresh) {
    CallbackFlags flags;
    auto ctx = makeContext(flags);

    EXPECT_TRUE(dispatchKey(kKeyF5, ctx));
    EXPECT_TRUE(flags.refresh);
}

// F11 -- fullscreen toggle

TEST(MainWindowKeyDispatchTest, F11InvokesOnFullscreenToggle) {
    CallbackFlags flags;
    auto ctx = makeContext(flags);

    EXPECT_TRUE(dispatchKey(kKeyF11, ctx));
    EXPECT_TRUE(flags.fullscreenToggle);
}

// Esc -- exits fullscreen only when in fullscreen

TEST(MainWindowKeyDispatchTest, EscapeExitsFullscreenWhenInFullscreen) {
    CallbackFlags flags;
    auto ctx = makeContext(flags, /*pageCount=*/3, /*isFullscreen=*/true);

    EXPECT_TRUE(dispatchKey(kKeyEscape, ctx));
    EXPECT_TRUE(flags.exitFullscreen);
}

TEST(MainWindowKeyDispatchTest, EscapeIsNoOpWhenNotInFullscreen) {
    CallbackFlags flags;
    auto ctx = makeContext(flags, /*pageCount=*/3, /*isFullscreen=*/false);

    // Returns false so Esc can propagate to child widgets (e.g. dialog
    // cancel, search entry clear).
    EXPECT_FALSE(dispatchKey(kKeyEscape, ctx));
    EXPECT_FALSE(flags.exitFullscreen);
}

// Unhandled keys

TEST(MainWindowKeyDispatchTest, UnhandledKeyReturnsFalse) {
    CallbackFlags flags;
    auto ctx = makeContext(flags);

    EXPECT_FALSE(dispatchKey(kKeyA, ctx));
    EXPECT_FALSE(flags.about);
    EXPECT_FALSE(flags.refresh);
    EXPECT_FALSE(flags.fullscreenToggle);
    EXPECT_FALSE(flags.exitFullscreen);
    EXPECT_EQ(flags.switchedToPage, -1);
}

// Null-callback safety

TEST(MainWindowKeyDispatchTest, AllKeysSafeWithAllNullCallbacks) {
    KeyDispatchContext ctx;
    ctx.pageCount = 3;
    ctx.isFullscreen = true;
    // Every std::function left default-empty.

    // F1/F2/F3/F4/F5/F11 should all return true without crashing.
    EXPECT_TRUE(dispatchKey(kKeyF1, ctx));
    EXPECT_TRUE(dispatchKey(kKeyF2, ctx));
    EXPECT_TRUE(dispatchKey(kKeyF3, ctx));
    EXPECT_TRUE(dispatchKey(kKeyF4, ctx));
    EXPECT_TRUE(dispatchKey(kKeyF5, ctx));
    EXPECT_TRUE(dispatchKey(kKeyF11, ctx));
    EXPECT_TRUE(dispatchKey(kKeyEscape, ctx));
}
