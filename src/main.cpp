#include <gtkmm.h>
#include "src/gtk/view/pages/DashboardPage.h"
#include "src/gtk/view/pages/ProductsPage.h"
#include "src/presenter/DashboardPresenter.h"
#include "src/presenter/ProductsPresenter.h"
#include "src/model/SimulatedModel.h"
#include "src/model/DatabaseManager.h"
#include <memory>
#include <iostream>

/// Main application window with UI loaded from .ui file
class MainWindow : public Gtk::ApplicationWindow {
public:
    MainWindow(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& builder)
        : Gtk::ApplicationWindow(cobject), builder_(builder) {
        
        // Start in fullscreen for industrial kiosk mode
        fullscreen();
        isFullscreen_ = true;
        
        // Load sidebar CSS
        loadSidebarCSS();
        
        // Setup keyboard shortcuts
        auto controller = Gtk::EventControllerKey::create();
        controller->signal_key_pressed().connect(
            sigc::mem_fun(*this, &MainWindow::on_key_pressed), false);
        add_controller(controller);
        
        // Get widgets from .ui file
        radioFullscreen_ = builder_->get_widget<Gtk::CheckButton>("radio_fullscreen");
        radioWindowed_ = builder_->get_widget<Gtk::CheckButton>("radio_windowed");
        dashboardContainer_ = builder_->get_widget<Gtk::Box>("dashboard_container");
        productsContainer_ = builder_->get_widget<Gtk::Box>("products_container");
        
        // Connect sidebar controls
        if (radioFullscreen_) {
            radioFullscreen_->signal_toggled().connect(
                sigc::mem_fun(*this, &MainWindow::on_display_mode_changed));
        }
        
        // Initialize database
        auto& db = app::model::DatabaseManager::instance();
        if (!db.initialize()) {
            std::cerr << "Failed to initialize database!\n";
        }
        
        // Create and add Dashboard page
        dashboardPresenter_ = std::make_shared<app::DashboardPresenter>();
        dashboardPage_ = Gtk::make_managed<app::view::DashboardPage>();
        dashboardPage_->initialize(dashboardPresenter_);
        dashboardPresenter_->initialize();
        
        if (dashboardContainer_) {
            dashboardContainer_->append(*dashboardPage_);
        }
        
        // Create and add Products page
        productsPresenter_ = std::make_shared<app::ProductsPresenter>();
        productsPage_ = Gtk::make_managed<app::view::ProductsPage>();
        productsPage_->initialize(productsPresenter_);
        productsPresenter_->initialize();
        
        if (productsContainer_) {
            productsContainer_->append(*productsPage_);
        }
        
        // Initialize Model with demo data
        app::model::SimulatedModel::instance().initializeDemoData();
        
        std::cout << "Industrial HMI Application Started\n";
        std::cout << "MVP Architecture Demonstration\n";
        std::cout << "- Dashboard: Equipment monitoring and control\n";
        std::cout << "- Products: Database integration with search\n";
        std::cout << "\nControls:\n";
        std::cout << "- Sidebar: Click Fullscreen/Windowed buttons\n";
        std::cout << "- F11: Toggle fullscreen/windowed mode\n";
        std::cout << "- ESC: Exit fullscreen\n";
    }
    
    ~MainWindow() override {
        std::cout << "Application closing...\n";
    }

private:
    void loadSidebarCSS() {
        auto cssProvider = Gtk::CssProvider::create();
        
        try {
            cssProvider->load_from_path("ui/sidebar.css");
            Gtk::StyleContext::add_provider_for_display(
                Gdk::Display::get_default(),
                cssProvider,
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
            );
        } catch (const Glib::Error& ex) {
            std::cerr << "Failed to load sidebar CSS: " << ex.what() << std::endl;
        }
    }
    
    void on_display_mode_changed() {
        if (radioFullscreen_ && radioFullscreen_->get_active()) {
            fullscreen();
            isFullscreen_ = true;
            std::cout << "Switched to fullscreen mode\n";
        } else {
            unfullscreen();
            isFullscreen_ = false;
            std::cout << "Switched to windowed mode (1920x1080)\n";
        }
    }
    
    bool on_key_pressed(guint keyval, guint, Gdk::ModifierType) {
        // F11: Toggle fullscreen
        if (keyval == GDK_KEY_F11) {
            toggleFullscreen();
            return true;
        }
        
        // ESC: Exit fullscreen
        if (keyval == GDK_KEY_Escape && isFullscreen_) {
            unfullscreen();
            isFullscreen_ = false;
            if (radioWindowed_) {
                radioWindowed_->set_active(true);
            }
            std::cout << "Switched to windowed mode\n";
            return true;
        }
        
        return false;
    }
    
    void toggleFullscreen() {
        if (isFullscreen_) {
            unfullscreen();
            isFullscreen_ = false;
            if (radioWindowed_) {
                radioWindowed_->set_active(true);
            }
            std::cout << "Switched to windowed mode (1920x1080)\n";
        } else {
            fullscreen();
            isFullscreen_ = true;
            if (radioFullscreen_) {
                radioFullscreen_->set_active(true);
            }
            std::cout << "Switched to fullscreen mode\n";
        }
    }

    Glib::RefPtr<Gtk::Builder> builder_;
    bool isFullscreen_ = false;
    
    // Widgets from .ui file
    Gtk::CheckButton* radioFullscreen_ = nullptr;
    Gtk::CheckButton* radioWindowed_ = nullptr;
    Gtk::Box* dashboardContainer_ = nullptr;
    Gtk::Box* productsContainer_ = nullptr;
    
    // Dashboard
    std::shared_ptr<app::DashboardPresenter> dashboardPresenter_;
    app::view::DashboardPage* dashboardPage_;
    
    // Products
    std::shared_ptr<app::ProductsPresenter> productsPresenter_;
    app::view::ProductsPage* productsPage_;
};

int main(int argc, char* argv[]) {
    auto app = Gtk::Application::create("com.industrial.hmi.demo");
    
    // Load UI from .ui file
    auto builder = Gtk::Builder::create_from_file("ui/main-window.ui");
    
    // Create window from builder
    auto window = Gtk::Builder::get_widget_derived<MainWindow>(builder, "main_window");
    
    if (!window) {
        std::cerr << "Failed to load main window from UI file!" << std::endl;
        return 1;
    }
    
    return app->make_window_and_run<MainWindow>(argc, argv, window);
}
