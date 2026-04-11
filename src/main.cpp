#include <gtkmm.h>
#include "src/gtk/view/pages/DashboardPage.h"
#include "src/gtk/view/pages/ProductsPage.h"
#include "src/presenter/DashboardPresenter.h"
#include "src/presenter/ProductsPresenter.h"
#include "src/model/SimulatedModel.h"
#include "src/model/DatabaseManager.h"
#include <memory>
#include <iostream>

/// Main application window with tab navigation
class MainWindow : public Gtk::Window {
public:
    MainWindow() {
        set_title("Industrial HMI - MVP Architecture Demo");
        set_default_size(1920, 1080);  // Full HD - industrial standard
        
        // Initialize database
        auto& db = app::model::DatabaseManager::instance();
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database!\n";
        }
        
        // Create Notebook (tabs)
        notebook_ = Gtk::make_managed<Gtk::Notebook>();
        notebook_->set_tab_pos(Gtk::PositionType::TOP);
        
        // Dashboard tab
        dashboardPresenter_ = std::make_shared<app::DashboardPresenter>();
        dashboardPage_ = Gtk::make_managed<app::view::DashboardPage>();
        dashboardPage_->initialize(dashboardPresenter_);
        dashboardPresenter_->initialize();
        notebook_->append_page(*dashboardPage_, "Dashboard");
        
        // Products tab
        productsPresenter_ = std::make_shared<app::ProductsPresenter>();
        productsPage_ = Gtk::make_managed<app::view::ProductsPage>();
        productsPage_->initialize(productsPresenter_);
        productsPresenter_->initialize();
        notebook_->append_page(*productsPage_, "Products Database");
        
        set_child(*notebook_);
        
        // Initialize Model with demo data
        app::model::SimulatedModel::instance().initializeDemoData();
        
        std::cout << "Industrial HMI Application Started\n";
        std::cout << "MVP Architecture Demonstration\n";
        std::cout << "- Dashboard: Equipment monitoring and control\n";
        std::cout << "- Products: Database integration with search\n";
    }
    
    ~MainWindow() override {
        std::cout << "Application closing...\n";
    }

private:
    Gtk::Notebook* notebook_;
    
    // Dashboard
    std::shared_ptr<app::DashboardPresenter> dashboardPresenter_;
    app::view::DashboardPage* dashboardPage_;
    
    // Products
    std::shared_ptr<app::ProductsPresenter> productsPresenter_;
    app::view::ProductsPage* productsPage_;
};

int main(int argc, char* argv[]) {
    auto app = Gtk::Application::create("com.industrial.hmi.demo");
    
    return app->make_window_and_run<MainWindow>(argc, argv);
}
