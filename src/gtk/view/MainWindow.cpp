// LoggerImpl must come before Boost/Windows headers
// to avoid wingdi.h ERROR macro conflict with LogLevel::ERROR
#include "src/core/LoggerImpl.h"

#include "MainWindow.h"
#include "src/gtk/view/DialogManager.h"
#include "src/gtk/view/MainWindowKeyDispatch.h"
#include "src/gtk/view/ThemeManager.h"
#include "src/gtk/view/pages/Page.h"
#include "src/gtk/view/pages/DashboardPage.h"
#include "src/gtk/view/pages/ProductsPage.h"
#include "src/gtk/view/pages/SettingsPage.h"
#include "src/gtk/view/widgets/AlertsPanel.h"
#include "src/gtk/view/widgets/SystemStatusBadge.h"
#include "src/gtk/view/widgets/LiveClock.h"
#include "src/presenter/AlertCenter.h"
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

    // Apply user's saved palette (empty = baseline industrial look).
    // Done after sidebar.css so the palette provider layers on top.
    app::view::ThemeManager::instance().setPalette(
        app::config::ConfigManager::instance().getPalette());

    // Setup keyboard shortcuts
    setupKeyboardShortcuts();

    // Create DialogManager (injected into pages)
    dialogManager_ = std::make_unique<app::view::DialogManager>(this);

    // AlertCenter lives alongside the window — DashboardPresenter
    // raises alerts into it from model callbacks, AlertsPanel (in
    // sidebar) renders the current snapshot.
    alertCenter_ = std::make_unique<app::presenter::AlertCenter>();

    // Build pages + presenters (also registers them with the Notebook)
    createAllPages();

    // Hook SettingsPage signals to MainWindow handlers
    wireSettingsSignals();

    // Sidebar / top-bar inhabitants (AlertsPanel, SystemStatusBadge,
    // LiveClock) + E-STOP wiring. Extracted so reloadLayout() can
    // rebuild them into a freshly-parsed .ui without duplicating the
    // ordering constraints (must come AFTER presenters exist so the
    // status-badge signal hookup resolves).
    buildSidebarWidgets();
    if (estopButton_ && dashboardPresenter_) {
        estopButton_->signal_clicked().connect([this]() {
            if (dashboardPresenter_) dashboardPresenter_->onStopClicked();
        });
    }

    // Blueprint routes logs through a top_bar popover; there's no
    // Settings "Show log panel" checkbox visible for the user there,
    // so auto-start the tail timer and keep verbose logging on so
    // clicking the log button always shows fresh output.
    if (logButton_) {
        verboseLogging_ = true;
        applyVerboseLogging(true);
    }

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

const char* MainWindow::chooseMainWindowUI(const std::string& palette) {
    // Palettes with a structurally different layout get their own .ui.
    if (palette == "blueprint") return app::config::defaults::kMainWindowBlueprintUI;
    if (palette == "right")     return app::config::defaults::kMainWindowRightUI;
    return app::config::defaults::kMainWindowUI;
}

Gtk::Box* MainWindow::parseLayoutUI() {
    const auto palette = app::config::ConfigManager::instance().getPalette();
    currentLayoutUI_ = chooseMainWindowUI(palette);
    // Keep the Builder alive as a member. Widget pointers returned by
    // get_widget<T>() are only valid while the Builder holds a ref,
    // until a parent adopts them via set_child/append. We release the
    // previous builder after adoption to free memory.
    pendingBuilder_ = Gtk::Builder::create_from_file(currentLayoutUI_);

    auto* rootContainer = pendingBuilder_->get_widget<Gtk::Box>("root_container");

    mainNotebook_          = pendingBuilder_->get_widget<Gtk::Notebook>("main_notebook");
    logPanel_              = pendingBuilder_->get_widget<Gtk::Box>("log_panel");
    logTextView_           = pendingBuilder_->get_widget<Gtk::TextView>("log_text_view");
    alertsContainer_       = pendingBuilder_->get_widget<Gtk::Box>("alerts_container");
    systemStatusContainer_ = pendingBuilder_->get_widget<Gtk::Box>("system_status_container");
    clockContainer_        = pendingBuilder_->get_widget<Gtk::Box>("clock_container");
    estopButton_           = pendingBuilder_->get_widget<Gtk::Button>("estop_button");
    // Only present in Blueprint layout (nullptr elsewhere).
    // get_widget<T>() emits a Gtk-CRITICAL when the id is missing,
    // even if the caller treats the return as "may be null". Use
    // get_object() + dynamic_cast so baseline layouts stay silent.
    if (auto obj = pendingBuilder_->get_object("log_button")) {
        logButton_ = dynamic_cast<Gtk::MenuButton*>(obj.get());
    } else {
        logButton_ = nullptr;
    }

    // Sidebar widgets we'll re-translate on language switch.
    appTitleLabel_    = pendingBuilder_->get_widget<Gtk::Label>("app_title");
    appSubtitleLabel_ = pendingBuilder_->get_widget<Gtk::Label>("app_subtitle");
    closeAppButton_   = pendingBuilder_->get_widget<Gtk::Button>("close_app_button");
    versionLabel_     = pendingBuilder_->get_widget<Gtk::Label>("version_label");
    authorLabel_      = pendingBuilder_->get_widget<Gtk::Label>("author_label");

    if (closeAppButton_) {
        closeAppButton_->signal_clicked().connect([this]() { close(); });
    }

    return rootContainer;
}

void MainWindow::loadUI() {
    if (auto* root = parseLayoutUI()) {
        set_child(*root);
        // set_child has adopted root — safe to drop the builder ref now.
        pendingBuilder_.reset();
    }
}

void MainWindow::buildSidebarWidgets() {
    // AlertsPanel mount — container is either in the sidebar (default
    // layout) or the footer strip (Blueprint layout). Widget ID is
    // the same either way, so this is layout-agnostic.
    if (alertsContainer_ && alertCenter_) {
        alertsPanel_ = Gtk::make_managed<app::view::AlertsPanel>(*alertCenter_);
        alertsContainer_->append(*alertsPanel_);
    }

    // System status LED badge — driven by DashboardPresenter's signal.
    if (systemStatusContainer_) {
        statusBadge_ = Gtk::make_managed<app::view::SystemStatusBadge>();
        systemStatusContainer_->append(*statusBadge_);
        if (dashboardPresenter_) {
            dashboardPresenter_->signalSystemStateChanged().connect(
                [this](int state) {
                    if (statusBadge_) statusBadge_->setState(state);
                });
        }
    }

    // Live clock.
    if (clockContainer_) {
        clock_ = Gtk::make_managed<app::view::LiveClock>();
        clockContainer_->append(*clock_);
    }
}

void MainWindow::refreshSidebarTranslations() {
    // main-window.ui is only parsed once at startup, so GtkBuilder's
    // translation machinery doesn't run again after a runtime language
    // switch. Re-assign the strings manually via `_()` — that hits the
    // freshly re-bound gettext catalog.
    if (appTitleLabel_)    appTitleLabel_->set_label(_("[BB] Industrial HMI"));
    if (appSubtitleLabel_) appSubtitleLabel_->set_label(_("MVP Architecture"));
    if (closeAppButton_)   closeAppButton_->set_label(_("Close Application"));
    if (versionLabel_)     versionLabel_->set_label(_("Version 1.0.0"));
    if (authorLabel_)      authorLabel_->set_label(_("Portfolio Demo"));
    if (alertsPanel_)      alertsPanel_->refreshTranslations();
    if (statusBadge_)      statusBadge_->refreshTranslations();
    if (estopButton_)      estopButton_->set_label(_("E-STOP"));
    if (clock_)            clock_->refreshTranslations();
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
    auto& logger = app::core::Application::instance().logger();

    // Dashboard
    dashboardPresenter_ = std::make_shared<app::DashboardPresenter>();
    dashboardPresenter_->setLogger(logger);
    if (alertCenter_) {
        dashboardPresenter_->setAlertCenter(*alertCenter_);
    }
    dashboardPage_ = Gtk::make_managed<app::view::DashboardPage>(*dialogManager_);
    dashboardPage_->initialize(dashboardPresenter_);
    registerPage(dashboardPage_);

    // Products
    productsPresenter_ = std::make_shared<app::ProductsPresenter>();
    productsPresenter_->setLogger(logger);
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
    // Palette change: if the new palette wants a different
    // main-window .ui we hot-reload the whole layout; otherwise the
    // CSS swap (already applied by SettingsPage) is all the user
    // needs. Deferred via signal_idle because the emitter (Settings
    // palette card) dies during reload.
    settingsPage_->signalPaletteChanged().connect(
        [this](Glib::ustring newPalette) {
            const std::string id = std::string(newPalette);
            const std::string toStore = (id == "industrial") ? "" : id;
            const std::string nextUI = chooseMainWindowUI(toStore);
            const bool needsRelayout = (nextUI != currentLayoutUI_);

            // Run CSS swap + (optional) relayout + Cairo repaint
            // together in one idle callback so all three commit in a
            // single compositor frame. Without this, the CSS change
            // would render immediately on the OLD layout, producing
            // a visible "hybrid" flash before the layout catches up.
            Glib::signal_idle().connect_once(
                [this, id, toStore, needsRelayout]() {
                    auto& tm = app::view::ThemeManager::instance();
                    // Palettes that ship only one mode by design —
                    // snap the Theme to their supported mode before
                    // loading the CSS, otherwise a freshly-selected
                    // Dracula on a Light canvas would unload itself
                    // again in ThemeManager::applyPalette().
                    if (id == "paper") {
                        tm.setTheme(app::view::ThemeManager::Theme::LIGHT);
                    } else if (id == "dracula" || id == "crt" ||
                               id == "blueprint" || id == "cockpit") {
                        tm.setTheme(app::view::ThemeManager::Theme::DARK);
                    }
                    tm.setPalette(toStore);
                    if (needsRelayout) {
                        reloadLayout();
                    }
                    // Keep the Dark/Light radios in sync with any
                    // forced-mode decision above.
                    if (settingsPage_) {
                        settingsPage_->syncWithRuntimeState(
                            /*fullscreen*/     isFullscreen_,
                            /*darkMode*/       tm.isDarkMode(),
                            /*autoRefresh*/    autoRefreshOn_,
                            /*verboseLogging*/ verboseLogging_);
                    }
                    onThemeApplied();  // tell Cairo widgets to repaint
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
    // GTK_WINDOW is a C macro that expands to a C-style cast through
    // void*; no way to avoid it without dropping to the raw GObject API.
    // NOLINTNEXTLINE(bugprone-casting-through-void, cppcoreguidelines-pro-type-cstyle-cast)
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

    // Diagnostic: does gettext actually return translated strings after
    // re-init? If "Settings" stays English, libintl is caching the old
    // catalog (MSYS2 libintl-8 is known to ignore _nl_msg_cat_cntr in
    // some builds). If it's translated, the issue lives in GtkBuilder.
    logger.debug(R"(i18n probe (post-init) _("Settings")="{}")", _("Settings"));
    logger.debug(R"(i18n probe (post-init) _("Dashboard")="{}")", _("Dashboard"));

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

    // 8) Re-translate sidebar widgets (branding + Close Application) —
    //    they were loaded by GtkBuilder at startup, not touched by the
    //    page rebuild above.
    refreshSidebarTranslations();

    //    Also re-translate retained alert content (history rows
    //    especially — active alerts will re-raise on the next model
    //    tick, but history is frozen until we poke it).
    if (alertCenter_) alertCenter_->retranslate();

    // 9) Re-wire the SystemStatusBadge to the fresh DashboardPresenter.
    //    The previous connection pointed at the now-destroyed instance.
    if (statusBadge_ && dashboardPresenter_) {
        dashboardPresenter_->signalSystemStateChanged().connect(
            [this](int state) {
                if (statusBadge_) statusBadge_->setState(state);
            });
    }
}

void MainWindow::reloadLayout() {
    auto& logger = app::core::Application::instance().logger();
    logger.info("Reloading main-window layout");

    const int activeTab = mainNotebook_ ? mainNotebook_->get_current_page() : 0;

    // 1) Stop background activity that would fire into soon-to-be-dead widgets.
    autoRefreshTimer_.disconnect();
    logRefreshConnection_.disconnect();

    // 2) Drop model callbacks so presenters about to die stop receiving.
    app::model::SimulatedModel::instance().clearCallbacks();

    // 3) Move focus out of whatever is inside the tree we're about to wipe.
    if (mainNotebook_ && mainNotebook_->get_n_pages() > 0) {
        mainNotebook_->set_current_page(0);
    }
    // NOLINTNEXTLINE(bugprone-casting-through-void, cppcoreguidelines-pro-type-cstyle-cast)
    gtk_window_set_focus(GTK_WINDOW(gobj()), nullptr);

    // 4) Release the presenters so old pages stop receiving notifications.
    //    The old page widgets still exist inside the old root and stay
    //    on screen until we swap. That keeps the UI visually stable
    //    during the rebuild.
    dashboardPresenter_.reset();
    productsPresenter_.reset();

    // 5) Parse the new .ui into a DETACHED subtree (no set_child yet).
    //    Member widget pointers — mainNotebook_, alertsContainer_, etc.
    //    — now point into the freshly-built, not-yet-installed tree.
    //    The raw Page/alerts/clock pointers still reference widgets in
    //    the currently-displayed old tree, so we zero them out: the
    //    corresponding widgets will be destroyed when set_child swaps
    //    the roots below.
    pages_.clear();
    dashboardPage_  = nullptr;
    productsPage_   = nullptr;
    settingsPage_   = nullptr;
    alertsPanel_    = nullptr;
    statusBadge_    = nullptr;
    clock_          = nullptr;
    Gtk::Box* newRoot = parseLayoutUI();

    // 6) Populate the new notebook + sidebar while still detached,
    //    so the user never sees a half-built layout.
    createAllPages();
    wireSettingsSignals();
    buildSidebarWidgets();
    if (estopButton_ && dashboardPresenter_) {
        estopButton_->signal_clicked().connect([this]() {
            if (dashboardPresenter_) dashboardPresenter_->onStopClicked();
        });
    }

    // 7) Atomic swap — GTK destroys the old root (and everything under
    //    it) in the same frame it installs the new, fully-populated
    //    root. No intermediate empty-notebook flash.
    if (newRoot) {
        set_child(*newRoot);
        pendingBuilder_.reset();
    }

    // 8) Restore runtime state + tab selection.
    if (settingsPage_) {
        settingsPage_->syncWithRuntimeState(
            /*fullscreen*/     isFullscreen_,
            /*darkMode*/       app::view::ThemeManager::instance().isDarkMode(),
            /*autoRefresh*/    autoRefreshOn_,
            /*verboseLogging*/ verboseLogging_);
    }
    if (autoRefreshOn_)  applyAutoRefresh(true);
    // Log tail timer: run if EITHER the user opted in via the
    // "Show log panel" checkbox OR the layout has a log popover
    // (Blueprint) that would otherwise be empty. Save + restore the
    // user's preference across applyVerboseLogging() because it
    // clobbers verboseLogging_ with whatever we pass in, and we
    // don't want the Blueprint-forced tail to leak into the
    // Settings checkbox after the next palette transition.
    const bool userWantsLogs = verboseLogging_;
    const bool runLogTail    = userWantsLogs || (logButton_ != nullptr);
    applyVerboseLogging(runLogTail);
    verboseLogging_ = userWantsLogs;

    if (mainNotebook_ && activeTab >= 0 &&
        activeTab < mainNotebook_->get_n_pages()) {
        mainNotebook_->set_current_page(activeTab);
    }

    refreshSidebarTranslations();
    if (alertCenter_) alertCenter_->retranslate();
}

// Handlers driven by SettingsPage signals

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

        app.logger().info("Log panel: enabled");

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
        app.logger().info("Log panel: disabled");
        logRefreshConnection_.disconnect();
        logPanel_->set_visible(false);
        // The Log Level combo in Settings controls the actual verbosity now;
        // this checkbox only toggles the panel visibility + live tail.
    }
}

// Keyboard shortcuts

bool MainWindow::onKeyPressed(guint keyval, guint, Gdk::ModifierType) {
    // The dispatch logic proper lives in MainWindowKeyDispatch.{h,cpp}
    // so it can be unit-tested without instantiating MainWindow. We
    // hand it a bundle of callbacks that close over the MainWindow
    // members each handler needs to touch.
    app::view::KeyDispatchContext ctx;
    ctx.pageCount = mainNotebook_ ? mainNotebook_->get_n_pages() : 0;
    ctx.onPageSwitch = [this](int index) {
        if (mainNotebook_) mainNotebook_->set_current_page(index);
    };
    ctx.onRefresh = [] {
        app::model::SimulatedModel::instance().tickSimulation();
    };
    ctx.onAbout = [this] {
        auto* about = new app::view::AboutDialog(*this);
        about->signal_close_request().connect([about]() {
            delete about;
            return false;
        }, false);
        about->present();
    };
    ctx.onFullscreenToggle = [this] { toggleFullscreen(); };
    ctx.isFullscreen = isFullscreen_;
    ctx.onExitFullscreen = [this] {
        unfullscreen();
        isFullscreen_ = false;
        if (settingsPage_) {
            settingsPage_->syncWithRuntimeState(
                /*fullscreen*/     false,
                /*darkMode*/       app::view::ThemeManager::instance().isDarkMode(),
                /*autoRefresh*/    autoRefreshOn_,
                /*verboseLogging*/ verboseLogging_);
        }
    };
    return app::view::dispatchKey(keyval, ctx);
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
