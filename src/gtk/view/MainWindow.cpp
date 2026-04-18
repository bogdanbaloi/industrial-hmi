// LoggerImpl must come before Boost/Windows headers
// to avoid wingdi.h ERROR macro conflict with LogLevel::ERROR
#include "src/core/LoggerImpl.h"

#include "MainWindow.h"
#include "src/gtk/view/DialogManager.h"
#include "src/gtk/view/ThemeManager.h"
#include "src/gtk/view/pages/Page.h"
#include "src/gtk/view/pages/DashboardPage.h"
#include "src/gtk/view/pages/ProductsPage.h"
#include "src/gtk/view/pages/SettingsPage.h"
#include "src/presenter/DashboardPresenter.h"
#include "src/presenter/ProductsPresenter.h"
#include "src/model/SimulatedModel.h"
#include "src/model/DatabaseManager.h"
#include "src/model/ModelContext.h"
#include "src/config/ConfigManager.h"
#include "src/config/config_defaults.h"
#include "src/core/Application.h"
#include "src/core/ExceptionHandler.h"
#include "src/core/i18n.h"
#include "src/gtk/view/AboutDialog.h"
#include <fstream>


MainWindow::MainWindow()
    : refreshIntervalMs_(app::config::defaults::kAutoRefreshIntervalMs) {
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

    // Apply custom styling (legacy sidebar CSS)
    loadSidebarCSS();

    // Setup keyboard shortcuts
    setupKeyboardShortcuts();

    // Create DialogManager (injected into pages)
    dialogManager_ = std::make_unique<app::view::DialogManager>(this);

    // Build pages + presenters (also registers them with the Notebook)
    createAllPages();

    // Hook SettingsPage signals to MainWindow handlers
    wireSettingsSignals();

    // Start in fullscreen (industrial kiosk mode)
    fullscreen();
    isFullscreen_ = true;

    settingsPage_->syncWithRuntimeState(
        /*fullscreen*/     isFullscreen_,
        /*darkMode*/       app::view::ThemeManager::instance().isDarkMode(),
        /*autoRefresh*/    autoRefreshOn_,
        /*verboseLogging*/ verboseLogging_);

    // Kick off the initial auto-refresh timer (delayed so pages are ready).
    Glib::signal_timeout().connect_once([this]() {
        if (autoRefreshOn_) {
            applyAutoRefresh(true);
        }
    }, app::config::defaults::kAutoRefreshStartDelayMs);

    appLogger.info("Window initialized - Theme: {}",
        app::view::ThemeManager::instance().isDarkMode() ? "Dark" : "Light");
}

MainWindow::~MainWindow() = default;

void MainWindow::loadUI() {
    auto builder = Gtk::Builder::create_from_file(app::config::defaults::kMainWindowUI);

    auto* rootContainer = builder->get_widget<Gtk::Box>("root_container");
    if (rootContainer) {
        set_child(*rootContainer);
    }

    mainNotebook_ = builder->get_widget<Gtk::Notebook>("main_notebook");
    logPanel_     = builder->get_widget<Gtk::Box>("log_panel");
    logTextView_  = builder->get_widget<Gtk::TextView>("log_text_view");

    // Close Application button (sidebar footer)
    auto* closeAppBtn = builder->get_widget<Gtk::Button>("close_app_button");
    if (closeAppBtn) {
        closeAppBtn->signal_clicked().connect([this]() { close(); });
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
        app::core::Application::instance().logger().error(
            "Failed to load sidebar CSS: {}", ex.what());
    }
}

void MainWindow::setupKeyboardShortcuts() {
    auto controller = Gtk::EventControllerKey::create();
    controller->signal_key_pressed().connect(
        sigc::mem_fun(*this, &MainWindow::onKeyPressed), false);
    add_controller(controller);
}

void MainWindow::registerPage(app::view::Page* page) {
    if (!page || !mainNotebook_) return;
    auto* label = Gtk::make_managed<Gtk::Label>(page->pageTitle());
    mainNotebook_->append_page(*page, *label);
    pages_.push_back(page);
}

void MainWindow::createAllPages() {
    // Dashboard
    dashboardPresenter_ = std::make_shared<app::DashboardPresenter>();
    dashboardPage_ = Gtk::make_managed<app::view::DashboardPage>(*dialogManager_);
    dashboardPage_->initialize(dashboardPresenter_);
    registerPage(dashboardPage_);

    // Products
    productsPresenter_ = std::make_shared<app::ProductsPresenter>();
    productsPage_ = Gtk::make_managed<app::view::ProductsPage>(*dialogManager_);
    productsPage_->initialize(productsPresenter_);
    registerPage(productsPage_);

    // Settings (no presenter — pure view over ConfigManager/ThemeManager/Logger)
    settingsPage_ = Gtk::make_managed<app::view::SettingsPage>(*dialogManager_);
    registerPage(settingsPage_);

    dashboardPresenter_->initialize();
    productsPresenter_->initialize();

    app::model::SimulatedModel::instance().initializeDemoData();
}

void MainWindow::clearPages() {
    if (!mainNotebook_) return;

    // remove_page drops the Notebook's ref; Gtk::make_managed widgets that
    // nobody else holds a ref to get destroyed here.
    while (mainNotebook_->get_n_pages() > 0) {
        mainNotebook_->remove_page(-1);
    }
    pages_.clear();
    dashboardPage_ = nullptr;
    productsPage_  = nullptr;
    settingsPage_  = nullptr;
}

void MainWindow::wireSettingsSignals() {
    if (!settingsPage_) return;

    settingsPage_->signalDisplayModeChanged().connect(
        sigc::mem_fun(*this, &MainWindow::applyDisplayMode));
    settingsPage_->signalThemeChanged().connect(
        sigc::mem_fun(*this, &MainWindow::onThemeApplied));
    settingsPage_->signalAutoRefreshToggled().connect(
        sigc::mem_fun(*this, &MainWindow::applyAutoRefresh));
    settingsPage_->signalRefreshIntervalChanged().connect(
        sigc::mem_fun(*this, &MainWindow::applyRefreshInterval));
    settingsPage_->signalVerboseLoggingToggled().connect(
        sigc::mem_fun(*this, &MainWindow::applyVerboseLogging));
    // rebuildPages destroys the SettingsPage that emitted this signal, so
    // if we called it synchronously the signal emitter would unwind back
    // into a dead widget (Gtk-CRITICAL: gtk_widget_is_ancestor). Defer to
    // the next idle so the current handler returns cleanly first.
    settingsPage_->signalLanguageChangeRequested().connect(
        [this](Glib::ustring lang) {
            Glib::signal_idle().connect_once([this, lang]() {
                rebuildPages(lang);
            });
        });
}

void MainWindow::rebuildPages(const Glib::ustring& newLanguage) {
    auto& logger = app::core::Application::instance().logger();
    logger.info("Rebuilding pages for language: {}", std::string(newLanguage));

    const int activeTab = mainNotebook_ ? mainNotebook_->get_current_page() : 0;

    // 1) Stop background activity that would fire into pages we're about to
    //    destroy.
    autoRefreshTimer_.disconnect();
    logRefreshConnection_.disconnect();

    // 2) Drop the Model's callback list so old presenter lambdas can't be
    //    invoked after the presenters die.
    app::model::SimulatedModel::instance().clearCallbacks();

    // 3) Move focus out of the SettingsPage before tearing it down. The
    //    combo box that triggered the rebuild still owns keyboard focus;
    //    destroying its parent makes GTK's focus tracker walk a dead
    //    widget chain ("gtk_widget_is_ancestor: GTK_IS_WIDGET failed").
    //    Switching to tab 0 moves focus into Dashboard content, and the
    //    C gtk_window_set_focus(NULL) call clears any residual focus
    //    widget. (gtkmm's set_focus takes Widget&, it can't null-out.)
    if (mainNotebook_ && mainNotebook_->get_n_pages() > 0) {
        mainNotebook_->set_current_page(0);
    }
    gtk_window_set_focus(GTK_WINDOW(gobj()), nullptr);

    // 4) Tear down the Notebook (destroys pages via Gtk::make_managed ref
    //    drop) and release the presenters.
    clearPages();
    dashboardPresenter_.reset();
    productsPresenter_.reset();

    // 4) Re-point gettext at the new catalog. After this call, both `_()`
    //    evaluations and GtkBuilder's `translatable="yes"` processing
    //    resolve against the new language.
    app::core::initI18n(app::config::defaults::kLocaleDir,
                        std::string(newLanguage).c_str());

    // 5) Rebuild pages and rewire everything. initializeDemoData() is
    //    called from createAllPages, re-seeding the new presenters.
    createAllPages();
    wireSettingsSignals();

    // 6) Restore runtime state on the new Settings widgets and restart the
    //    auto-refresh timer if it was on.
    if (settingsPage_) {
        settingsPage_->syncWithRuntimeState(
            /*fullscreen*/     isFullscreen_,
            /*darkMode*/       app::view::ThemeManager::instance().isDarkMode(),
            /*autoRefresh*/    autoRefreshOn_,
            /*verboseLogging*/ verboseLogging_);
    }
    if (autoRefreshOn_) {
        applyAutoRefresh(true);
    }
    if (verboseLogging_) {
        applyVerboseLogging(true);
    }

    // 7) Stay on whatever tab the user was on.
    if (mainNotebook_ && activeTab >= 0 &&
        activeTab < mainNotebook_->get_n_pages()) {
        mainNotebook_->set_current_page(activeTab);
    }
}

// ----------------------------------------------------------------------------
// Handlers driven by SettingsPage signals
// ----------------------------------------------------------------------------

void MainWindow::applyDisplayMode(bool wantFullscreen) {
    auto& logger = app::core::Application::instance().logger();
    if (wantFullscreen) {
        fullscreen();
        isFullscreen_ = true;
        logger.info("Display mode: fullscreen");
    } else {
        unfullscreen();
        isFullscreen_ = false;
        logger.info("Display mode: windowed");
    }
}

void MainWindow::applyAutoRefresh(bool enabled) {
    autoRefreshOn_ = enabled;
    auto& logger = app::core::Application::instance().logger();

    autoRefreshTimer_.disconnect();

    if (enabled) {
        logger.info("Auto refresh enabled ({}ms)", refreshIntervalMs_);
        autoRefreshTimer_ = Glib::signal_timeout().connect([this]() {
            // Only tick while the Dashboard tab is visible
            if (mainNotebook_ && mainNotebook_->get_current_page() != 0) return true;
            app::model::SimulatedModel::instance().tickSimulation();
            return true;
        }, refreshIntervalMs_);
    } else {
        logger.info("Auto refresh disabled");
    }
}

void MainWindow::applyRefreshInterval(int intervalMs) {
    refreshIntervalMs_ = intervalMs;
    app::core::Application::instance().logger().info(
        "Refresh interval set to: {}ms", intervalMs);
    if (autoRefreshOn_) {
        applyAutoRefresh(true);  // re-tune timer
    }
}

void MainWindow::onThemeApplied() {
    // Dispatch the theme change to every registered page uniformly.
    for (auto* page : pages_) {
        if (page) page->onThemeChanged();
    }
}

void MainWindow::applyVerboseLogging(bool enabled) {
    verboseLogging_ = enabled;
    if (!logPanel_ || !logTextView_) return;

    auto& app = app::core::Application::instance();

    if (enabled) {
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

// ----------------------------------------------------------------------------
// Keyboard shortcuts
// ----------------------------------------------------------------------------

bool MainWindow::onKeyPressed(guint keyval, guint, Gdk::ModifierType) {
    // F1: About dialog
    if (keyval == GDK_KEY_F1) {
        auto* about = new app::view::AboutDialog(*this);
        about->signal_close_request().connect([about]() {
            delete about;
            return false;
        }, false);
        about->present();
        return true;
    }

    // F2 / F3 / F4: jump to the 1st/2nd/3rd registered page (generic)
    auto switchToPage = [this](int index) -> bool {
        if (!mainNotebook_) return false;
        if (index < 0 || index >= mainNotebook_->get_n_pages()) return false;
        mainNotebook_->set_current_page(index);
        return true;
    };
    if (keyval == GDK_KEY_F2) return switchToPage(0);
    if (keyval == GDK_KEY_F3) return switchToPage(1);
    if (keyval == GDK_KEY_F4) return switchToPage(2);

    // F5: manual refresh (advance the simulation one tick)
    if (keyval == GDK_KEY_F5) {
        app::model::SimulatedModel::instance().tickSimulation();
        return true;
    }

    // F11: Toggle fullscreen
    if (keyval == GDK_KEY_F11) {
        toggleFullscreen();
        return true;
    }

    // Esc: Exit fullscreen
    if (keyval == GDK_KEY_Escape && isFullscreen_) {
        unfullscreen();
        isFullscreen_ = false;
        if (settingsPage_) {
            settingsPage_->syncWithRuntimeState(
                /*fullscreen*/     false,
                /*darkMode*/       app::view::ThemeManager::instance().isDarkMode(),
                /*autoRefresh*/    autoRefreshOn_,
                /*verboseLogging*/ verboseLogging_);
        }
        return true;
    }

    return false;
}

void MainWindow::toggleFullscreen() {
    if (isFullscreen_) {
        unfullscreen();
        isFullscreen_ = false;
    } else {
        fullscreen();
        isFullscreen_ = true;
    }
    if (settingsPage_) {
        settingsPage_->syncWithRuntimeState(
            /*fullscreen*/     isFullscreen_,
            /*darkMode*/       app::view::ThemeManager::instance().isDarkMode(),
            /*autoRefresh*/    autoRefreshOn_,
            /*verboseLogging*/ verboseLogging_);
    }
}
