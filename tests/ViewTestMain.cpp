// Custom main() for View-layer tests that require GTK initialization.
//
// GTK widgets can only be constructed after gtk_init(). On CI (Linux)
// this needs a virtual framebuffer (xvfb-run). On Windows the native
// Win32 backend works without any display workaround.

#include <gtk/gtk.h>
#include <gtest/gtest.h>

int main(int argc, char** argv) {
    gtk_init();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
