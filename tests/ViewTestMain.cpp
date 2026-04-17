// Custom main() for View-layer tests that require GTK + gtkmm initialization.
//
// Gtk::Application::create() registers all gtkmm type wrappers so
// Gtk::Builder::create_from_file() can map GObject types to C++ classes.
// We never call app->run() — just creating the Application is enough.
//
// On CI (Linux) this needs a virtual framebuffer (xvfb-run).

#include <gtkmm.h>
#include <gtest/gtest.h>

int main(int argc, char** argv) {
    auto app = Gtk::Application::create("com.test.industrial-hmi");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
