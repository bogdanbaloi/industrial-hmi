#include "MainWindow.h"
#include "src/gtk/view/DialogManager.h"
#include "src/gtk/view/ThemeManager.h"
#include "src/gtk/view/pages/DashboardPage.h"
#include "src/gtk/view/pages/ProductsPage.h"
#include "src/presenter/DashboardPresenter.h"
#include "src/presenter/ProductsPresenter.h"
#include "src/model/SimulatedModel.h"
#include "src/model/DatabaseManager.h"
#include "src/config/ConfigManager.h"
#include <iostream>

MainWindow::MainWindow() {
    // Initialize configuration from JSON
    auto& config = config::ConfigManager::instance();
    config.initialize("config/app-config.json");
    
    // Set window title from config
    set_title(config.getWindowTitle());
    
    // Set window icon
    set_icon_name(config.getWindowIconName());
    
    // Load UI layout from .ui file
    loadUI();
    
    // Initialize theme system (Adwaita Dark/Light with Design Tokens)
    view::ThemeManager::instance().initialize(this);
    
    // Apply custom styling (legacy sidebar CSS - being replaced by adwaita-theme.css)
    loadSidebarCSS();
    
    // Setup keyboard shortcuts (F11, ESC)
    setupKeyboardShortcuts();
    
    // Connect sidebar controls to handlers
    connectSignals();
    
    // Create DialogManager (injected into pages)
    dialogManager_ = std::make_unique<view::DialogManager>(this);
    
    // Initialize MVP pages
    initializePages();
    
    // Start in fullscreen (industrial kiosk mode)
    fullscreen();
    isFullscreen_ = true;
    
    std::cout << "Industrial HMI Application Started\n";
    std::cout << "MVP Architecture Demonstration\n";
    std::cout << "- Dashboard: Equipment monitoring and control\n";
    std::cout << "- Products: Database integration with CRUD operations\n";
    std::cout << "\n🎨 Theme: Adwaita " 
              << (view::ThemeManager::instance().isDarkMode() ? "Dark" : "Light") 
              << " (Design Tokens)\n";
    std::cout << "\nControls:\n";
    std::cout << "- Sidebar: Click Fullscreen/Windowed buttons\n";
    std::cout << "- F11: Toggle fullscreen/windowed mode\n";
    std::cout << "- ESC: Exit fullscreen\n";
    std::cout << "\nProducts Page:\n";
    std::cout << "- [+ Add New Product]: Create new products\n";
    std::cout << "- [View Details]: Double-click or button\n";
    std::cout << "- [Delete]: Soft delete with confirmation\n";
    std::cout << "- [Edit]: Update existing products\n";
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
    radioDark_ = builder->get_widget<Gtk::CheckButton>("radio_dark");
    radioLight_ = builder->get_widget<Gtk::CheckButton>("radio_light");
    dashboardContainer_ = builder->get_widget<Gtk::Box>("dashboard_container");
    productsContainer_ = builder->get_widget<Gtk::Box>("products_container");
    
    // Exit Fullscreen button
    auto* exitFullscreenBtn = builder->get_widget<Gtk::Button>("exit_fullscreen_button");
    if (exitFullscreenBtn) {
        exitFullscreenBtn->signal_clicked().connect([this]() {
            unfullscreen();
            if (radioWindowed_) {
                radioWindowed_->set_active(true);
            }
        });
    }
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
    
    // Connect theme radio buttons to theme handler
    if (radioDark_) {
        radioDark_->signal_toggled().connect(
            sigc::mem_fun(*this, &MainWindow::onThemeChanged));
    }
}

void MainWindow::initializePages() {
    // Initialize database
    auto& db = app::model::DatabaseManager::instance();
    if (!db.initialize()) {
        std::cerr << "Failed to initialize database!\n";
    }
    
    // Create Dashboard page (MVP pattern with Dependency Injection)
    dashboardPresenter_ = std::make_shared<app::DashboardPresenter>();
    dashboardPage_ = Gtk::make_managed<app::view::DashboardPage>(*dialogManager_);
    dashboardPage_->initialize(dashboardPresenter_);
    dashboardPresenter_->initialize();
    
    if (dashboardContainer_) {
        dashboardContainer_->append(*dashboardPage_);
    }
    
    // Create Products page (MVP pattern with Dependency Injection)
    productsPresenter_ = std::make_shared<app::ProductsPresenter>();
    productsPage_ = Gtk::make_managed<app::view::ProductsPage>(*dialogManager_);
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

void MainWindow::onThemeChanged() {
    if (radioDark_ && radioDark_->get_active()) {
        view::ThemeManager::instance().setTheme(view::ThemeManager::Theme::DARK);
        std::cout << "🌙 Switched to Dark Mode\n";
    } else if (radioLight_ && radioLight_->get_active()) {
        view::ThemeManager::instance().setTheme(view::ThemeManager::Theme::LIGHT);
        std::cout << "☀️  Switched to Light Mode\n";
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
