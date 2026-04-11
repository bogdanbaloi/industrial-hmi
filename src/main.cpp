#include <gtkmm.h>
#include "src/gtk/view/MainWindow.h"

/// Application entry point
/// 
/// Single Responsibility: Create application and run main window
int main(int argc, char* argv[]) {
    auto app = Gtk::Application::create("com.industrial.hmi.demo");
    return app->make_window_and_run<MainWindow>(argc, argv);
}
