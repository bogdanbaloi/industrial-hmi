#include "MainWindow.h"
#include "src/gtk/view/pages/DashboardPage.h"
#include "src/gtk/view/pages/ProductsPage.h"
#include "src/presenter/DashboardPresenter.h"
#include "src/presenter/ProductsPresenter.h"
#include "src/model/SimulatedModel.h"
#include "src/model/DatabaseManager.h"
#include <iostream>

MainWindow::MainWindow() {
    // Load UI layout from .ui file
    loadUI();
    
    // Apply custom styling
    loadSidebarCSS();
    
    // Setup keyboard shortcuts (F11, ESC)
    setupKeyboardShortcuts();
    
    // Connect sidebar controls to handlers
    connectSignals();
    
    // Initialize MVP pages
    initializePages();
    
    // Start in fullscreen (industrial kiosk mode)
    fullscreen();
    isFullscreen_ = true;
    
    std::cout << "Industrial HMI Application Started\n";
    std::cout << "MVP Architecture Demonstration\n";
    std::cout << "- Dashboard: Equipment monitoring and control\n";
    std::cout << "- Products: Database integration with search\n";
    std::cout << "\nControls:\n";
    std::cout << "- Sidebar: Click Fullscreen/Windowed buttons\n";
    std::cout << "- F11: Toggle fullscreen/windowed mode\n";
    std::cout << "- ESC: Exit fullscreen\n";
}

MainWindow::~MainWindow() {
    std::cout << "Application closing...\n";
}

void MainWindow::loadUI() {
    // Load UI definition from XML file
    auto builder = Gtk::Builder::create_from_file("ui/main-window.ui");
    
    // Get root container and set as window child
    auto* rootContainer = builder->get_widget<Gtk::Box>("root_container");
    if (rootContainer) {
        set_child(*rootContainer);
    }
    
    // Get widget references from builder
    radioFullscreen_ = builder->get_widget<Gtk::CheckButton>("radio_fullscreen");
    radioWindowed_ = builder->get_widget<Gtk::CheckButton>("radio_windowed");
    dashboardContainer_ = builder->get_widget<Gtk::Box>("dashboard_container");
    productsContainer_ = builder->get_widget<Gtk::Box>("products_container");
}

void MainWindow::loadSidebarCSS() {
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

void MainWindow::setupKeyboardShortcuts() {
    auto controller = Gtk::EventControllerKey::create();
    controller->signal_key_pressed().connect(
        sigc::mem_fun(*this, &MainWindow::onKeyPressed), false);
    add_controller(controller);
}

void MainWindow::connectSignals() {
    // Connect sidebar radio buttons to display mode handler
    if (radioFullscreen_) {
        radioFullscreen_->signal_toggled().connect(
            sigc::mem_fun(*this, &MainWindow::onDisplayModeChanged));
    }
}

void MainWindow::initializePages() {
    // Initialize database
    auto& db = app::model::DatabaseManager::instance();
    if (!db.initialize()) {
        std::cerr << "Failed to initialize database!\n";
    }
    
    // Create Dashboard page (MVP pattern)
    dashboardPresenter_ = std::make_shared<app::DashboardPresenter>();
    dashboardPage_ = Gtk::make_managed<app::view::DashboardPage>();
    dashboardPage_->initialize(dashboardPresenter_);
    dashboardPresenter_->initialize();
    
    if (dashboardContainer_) {
        dashboardContainer_->append(*dashboardPage_);
    }
    
    // Create Products page (MVP pattern)
    productsPresenter_ = std::make_shared<app::ProductsPresenter>();
    productsPage_ = Gtk::make_managed<app::view::ProductsPage>();
    productsPage_->initialize(productsPresenter_);
    productsPresenter_->initialize();
    
    if (productsContainer_) {
        productsContainer_->append(*productsPage_);
    }
    
    // Initialize model with demo data
    app::model::SimulatedModel::instance().initializeDemoData();
}

void MainWindow::onDisplayModeChanged() {
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

bool MainWindow::onKeyPressed(guint keyval, guint, Gdk::ModifierType) {
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

void MainWindow::toggleFullscreen() {
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
