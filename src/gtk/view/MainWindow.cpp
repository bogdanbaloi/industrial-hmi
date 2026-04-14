// LoggerImpl must come before Boost/Windows headers
// to avoid wingdi.h ERROR macro conflict with LogLevel::ERROR
#include "src/core/LoggerImpl.h"

#include "MainWindow.h"
#include "src/gtk/view/DialogManager.h"
#include "src/gtk/view/ThemeManager.h"
#include "src/gtk/view/pages/DashboardPage.h"
#include "src/gtk/view/pages/ProductsPage.h"
#include "src/presenter/DashboardPresenter.h"
#include "src/presenter/ProductsPresenter.h"
#include "src/model/SimulatedModel.h"
#include "src/model/DatabaseManager.h"
#include "src/model/ModelContext.h"
#include "src/config/ConfigManager.h"
#include "src/config/config_defaults.h"
#include "src/core/Application.h"
#include "src/core/ExceptionHandler.h"
#include <fstream>


MainWindow::MainWindow() {
    // Get logger from Application (already initialized)
    auto& appLogger = app::core::Application::instance().logger();

    // Initialize exception handler with application logger
    exceptionHandler_ = std::make_unique<app::core::StandardExceptionHandler>(appLogger);

    // Set window properties from config
    auto& config = app::config::ConfigManager::instance();
    set_title(config.getWindowTitle());
    set_icon_name(config.getWindowIconName());
    
    // Load UI layout from .ui file
    loadUI();
    
    // Initialize theme system (Adwaita Dark/Light with Design Tokens)
    app::view::ThemeManager::instance().initialize(this);
    
    // Apply custom styling (legacy sidebar CSS - being replaced by adwaita-theme.css)
    loadSidebarCSS();
    
    // Setup keyboard shortcuts (F11, ESC)
    setupKeyboardShortcuts();
    
    // Connect sidebar controls to handlers
    connectSignals();
    
    // Create DialogManager (injected into pages)
    dialogManager_ = std::make_unique<app::view::DialogManager>(this);
    
    // Initialize MVP pages
    initializePages();

    // Start auto refresh after UI is ready (delayed)
    Glib::signal_timeout().connect_once([this]() {
        if (checkAutoRefresh_ && checkAutoRefresh_->get_active()) {
            onAutoRefreshToggled();
        }
    }, app::config::defaults::kAutoRefreshStartDelayMs);

    // Start in fullscreen (industrial kiosk mode)
    fullscreen();
    isFullscreen_ = true;
    
    appLogger.info("Window initialized - Theme: {}",
        app::view::ThemeManager::instance().isDarkMode() ? "Dark" : "Light");
}

MainWindow::~MainWindow() = default;

void MainWindow::loadUI() {
    // Load UI definition from XML file
    auto builder = Gtk::Builder::create_from_file(app::config::defaults::kMainWindowUI);
    
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
    mainNotebook_ = builder->get_widget<Gtk::Notebook>("main_notebook");
    checkAutoRefresh_ = builder->get_widget<Gtk::CheckButton>("check_auto_refresh");
    checkShowLogs_ = builder->get_widget<Gtk::CheckButton>("check_show_logs");
    logPanel_ = builder->get_widget<Gtk::Box>("log_panel");
    logTextView_ = builder->get_widget<Gtk::TextView>("log_text_view");
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
        cssProvider->load_from_path(app::config::defaults::kSidebarCSS);
        Gtk::StyleContext::add_provider_for_display(
            Gdk::Display::get_default(),
            cssProvider,
            GTK_STYLE_PROVIDER_PRIORITY_USER
        );
    } catch (const Glib::Error& ex) {
        app::core::Application::instance().logger().error("Failed to load sidebar CSS: {}", ex.what());
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
    if (radioLight_) {
        radioLight_->signal_toggled().connect(
            sigc::mem_fun(*this, &MainWindow::onThemeChanged));
    }
    if (checkAutoRefresh_) {
        checkAutoRefresh_->property_active().signal_changed().connect(
            sigc::mem_fun(*this, &MainWindow::onAutoRefreshToggled));
    }
    if (checkShowLogs_) {
        checkShowLogs_->property_active().signal_changed().connect(
            sigc::mem_fun(*this, &MainWindow::onShowLogsToggled));
    } else {
        app::core::Application::instance().logger().warn("Show Logs checkbox not found in UI");
    }
}

void MainWindow::initializePages() {
    dashboardPresenter_ = std::make_shared<app::DashboardPresenter>();
    dashboardPage_ = Gtk::make_managed<app::view::DashboardPage>(*dialogManager_);
    dashboardPage_->initialize(dashboardPresenter_);

    productsPresenter_ = std::make_shared<app::ProductsPresenter>();
    productsPage_ = Gtk::make_managed<app::view::ProductsPage>(*dialogManager_);
    productsPage_->initialize(productsPresenter_);

    if (dashboardContainer_) dashboardContainer_->append(*dashboardPage_);
    if (productsContainer_) productsContainer_->append(*productsPage_);

    dashboardPresenter_->initialize();
    productsPresenter_->initialize();

    app::model::SimulatedModel::instance().initializeDemoData();
}

void MainWindow::onDisplayModeChanged() {
    auto& logger = app::core::Application::instance().logger();
    if (radioFullscreen_ && radioFullscreen_->get_active()) {
        fullscreen();
        isFullscreen_ = true;
        logger.info("Display mode: fullscreen");
    } else {
        unfullscreen();
        isFullscreen_ = false;
        logger.info("Display mode: windowed");
    }
}

void MainWindow::onAutoRefreshToggled() {
    if (!checkAutoRefresh_) return;

    auto& logger = app::core::Application::instance().logger();

    if (checkAutoRefresh_->get_active()) {
        logger.info("Auto refresh enabled");

        autoRefreshTimer_ = Glib::signal_timeout().connect([this]() {
            if (mainNotebook_ && mainNotebook_->get_current_page() != 0) return true;
            app::model::SimulatedModel::instance().tickSimulation();
            return true;
        }, app::config::defaults::kAutoRefreshIntervalMs);
    } else {
        logger.info("Auto refresh disabled");
        autoRefreshTimer_.disconnect();
    }
}

void MainWindow::onThemeChanged() {
    // signal_toggled fires for both deactivation and activation;
    // only act on the newly active radio button
    if (radioDark_ && radioDark_->get_active()) {
        app::view::ThemeManager::instance().setTheme(app::view::ThemeManager::Theme::DARK);
        app::core::Application::instance().logger().info("Theme: dark");
    }
    if (radioLight_ && radioLight_->get_active()) {
        app::view::ThemeManager::instance().setTheme(app::view::ThemeManager::Theme::LIGHT);
        app::core::Application::instance().logger().info("Theme: light");
    }
}

void MainWindow::onShowLogsToggled() {
    if (!checkShowLogs_ || !logPanel_ || !logTextView_) return;

    auto& app = app::core::Application::instance();

    if (checkShowLogs_->get_active()) {
        logPanel_->set_visible(true);
        logTextView_->get_buffer()->set_text("");

        // Mark current file position - only show new logs from now
        auto logPath = app::config::ConfigManager::instance().getLogFilePath();
        app.logger().flush();
        lastLogSize_ = std::filesystem::file_size(logPath);

        app.logger().setLevel(app::core::LogLevel::DEBUG);
        app.logger().info("Verbose logging enabled");

        // Check for new log content periodically
        logRefreshConnection_ = Glib::signal_timeout().connect([this]() {
            auto logPath = app::config::ConfigManager::instance().getLogFilePath();
            std::size_t currentSize = 0;
            try {
                currentSize = std::filesystem::file_size(logPath);
            } catch (...) {
                return true;
            }

            if (currentSize > lastLogSize_) {
                std::ifstream file(logPath);
                if (file.is_open()) {
                    file.seekg(static_cast<std::streamoff>(lastLogSize_));
                    std::string newContent((std::istreambuf_iterator<char>(file)),
                                            std::istreambuf_iterator<char>());
                    if (!newContent.empty()) {
                        logTextView_->get_buffer()->insert(
                            logTextView_->get_buffer()->end(), newContent);
                    }
                }
                lastLogSize_ = currentSize;
            }
            return true;
        }, app::config::defaults::kLogPanelRefreshMs);
    } else {
        app.logger().info("Verbose logging disabled");
        logRefreshConnection_.disconnect();
        logPanel_->set_visible(false);

        auto level = app::core::parseLogLevel(
            app::config::ConfigManager::instance().getLogLevel());
        app.logger().setLevel(level);
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
    } else {
        fullscreen();
        isFullscreen_ = true;
        if (radioFullscreen_) {
            radioFullscreen_->set_active(true);
        }
    }
}
