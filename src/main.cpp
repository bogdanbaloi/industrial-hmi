#include <gtkmm.h>
#include "src/gtk/view/pages/DashboardPage.h"
#include "src/presenter/DashboardPresenter.h"
#include "src/model/SimulatedModel.h"
#include <memory>
#include <iostream>

/// Main application window
class MainWindow : public Gtk::Window {
public:
    MainWindow() {
        set_title("Industrial HMI - MVP Architecture Demo");
        set_default_size(1920, 1080);  // Full HD - industrial standard
        
        // Create Presenter
        presenter_ = std::make_shared<app::DashboardPresenter>();
        
        // Create View (DashboardPage)
        dashboardPage_ = Gtk::make_managed<app::view::DashboardPage>();
        
        // Connect View to Presenter
        dashboardPage_->initialize(presenter_);
        
        // Initialize Presenter (subscribes to Model)
        presenter_->initialize();
        
        // Set dashboard as window content
        set_child(*dashboardPage_);
        
        // Initialize Model with demo data
        app::model::SimulatedModel::instance().initializeDemoData();
        
        std::cout << "Industrial HMI Application Started\n";
        std::cout << "MVP Architecture Demonstration\n";
        std::cout << "This is a functional demo with simulated data\n";
    }
    
    ~MainWindow() override {
        std::cout << "Application closing...\n";
    }

private:
    std::shared_ptr<app::DashboardPresenter> presenter_;
    app::view::DashboardPage* dashboardPage_;
};

int main(int argc, char* argv[]) {
    auto app = Gtk::Application::create("com.industrial.hmi.demo");
    
    return app->make_window_and_run<MainWindow>(argc, argv);
}
