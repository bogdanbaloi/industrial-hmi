// LoggerImpl must come before Boost/Windows headers
// to avoid wingdi.h ERROR macro conflict with LogLevel::ERROR
#include "src/core/LoggerImpl.h"

#include "MainWindow.h"
#include "src/gtk/view/DialogManager.h"
#include "src/gtk/view/MainWindowKeyDispatch.h"
#include "src/gtk/view/ThemeManager.h"
#include "src/auth/AuthService.h"
#include "src/auth/Role.h"
#include "src/auth/Session.h"
#include "src/gtk/view/pages/Page.h"
#include "src/gtk/view/pages/AuditLogPage.h"
#include "src/gtk/view/pages/UsersPage.h"
#include "src/gtk/view/LoginDialog.h"
#include "src/presenter/UsersPresenter.h"
#include "src/gtk/view/pages/DashboardPage.h"
#include "src/gtk/view/pages/MultiStationDashboardPage.h"
#include "src/gtk/view/pages/HistoryPage.h"
#include "src/gtk/view/pages/ProductsPage.h"
#include "src/gtk/view/pages/SettingsPage.h"
#include "src/gtk/view/widgets/AlertsPanel.h"
#include "src/gtk/view/widgets/UserBadge.h"
#include "src/gtk/view/widgets/BackendHealthBar.h"
#include "src/gtk/view/widgets/SystemStatusBadge.h"
#include "src/gtk/view/widgets/LiveClock.h"
#include "src/presenter/BackendHealthPresenter.h"
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

#ifdef INDUSTRIAL_HMI_HAS_ML_PLUGIN
#  include "src/gtk/view/pages/QualityInspectionPage.h"
#  include "src/ml/ImageDecoder.h"
#  include "src/ml/OnnxImageClassifier.h"
#  include "src/presenter/QualityInspectionPresenter.h"
#endif

#include <filesystem>
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

    // AlertCenter lives alongside the window -- DashboardPresenter
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

MainWindow::~MainWindow() {
    // Disconnect the backend-health timer BEFORE the presenter goes
    // out of scope -- otherwise a fire in flight after teardown would
    // dereference a destroyed presenter.
    if (backendHealthTimer_.connected()) {
        backendHealthTimer_.disconnect();
    }
    if (backendHealthPresenter_) {
        backendHealthPresenter_->removeObserver(this);
    }
}

const char* MainWindow::chooseMainWindowUI(const std::string& palette) {
    // Multi-station mode does NOT override layout -- it reuses the
    // palette's existing main-window .ui so the full sidebar (with
    // UserBadge / AlertsPanel / BackendHealthBar) stays visible.
    // The Dashboard tab content is swapped in createAllPages: when
    // a secondary model is wired in, a MultiStationDashboardPage is
    // mounted instead of the single DashboardPage. The two
    // dashboards inside are aggressively compacted (see
    // DashboardPage::setCompact) so they fit alongside the full
    // sidebar without overflow. See ADR-0011.
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
    systemStatusContainer_  = pendingBuilder_->get_widget<Gtk::Box>("system_status_container");
    backendHealthContainer_ = pendingBuilder_->get_widget<Gtk::Box>("backend_health_container");
    // `user_container` is only present in the redesigned default
    // layout (main-window.ui); Blueprint + Right variants still
    // stack UserBadge into system_status_container. Use the
    // optional-id pattern (object lookup + dynamic_cast) so the
    // missing id doesn't emit a Gtk-CRITICAL on those layouts.
    if (auto obj = pendingBuilder_->get_object("user_container")) {
        userContainer_ = dynamic_cast<Gtk::Box*>(obj.get());
    } else {
        userContainer_ = nullptr;
    }
    clockContainer_         = pendingBuilder_->get_widget<Gtk::Box>("clock_container");
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
        // set_child has adopted root -- safe to drop the builder ref now.
        pendingBuilder_.reset();
    }
}

void MainWindow::buildSidebarWidgets() {
    // AlertsPanel mount -- container is either in the sidebar (default
    // layout) or the footer strip (Blueprint layout). Widget ID is
    // the same either way, so this is layout-agnostic.
    if (alertsContainer_ && alertCenter_) {
        alertsPanel_ = Gtk::make_managed<app::view::AlertsPanel>(*alertCenter_);
        alertsContainer_->append(*alertsPanel_);
    }

    // UserBadge -- prefer the dedicated `user_container` (redesigned
    // default layout). Falls back to `system_status_container` for
    // Blueprint + Right layouts that haven't been migrated yet.
    if (auto* svc = app::core::Application::instance().authService();
        svc != nullptr) {
        auto* session =
            app::core::Application::instance().authSession();
        if (session != nullptr) {
            userBadge_ = Gtk::make_managed<app::view::UserBadge>(
                *svc, *session,
                app::core::Application::instance().usersPresenter());
            userBadge_->onSignOut(
                [this]() { handleSignOut(); });

            Gtk::Box* host = userContainer_
                ? userContainer_
                : systemStatusContainer_;
            if (host != nullptr) {
                host->append(*userBadge_);
            }
        }
    }

    // System status LED badge -- driven by DashboardPresenter's signal.
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

    // Backend-health bar -- mounted in its own container so it reads as
    // a separate sidebar block from the system-state badge above. Only
    // built when an IntegrationManager was injected via Application; a
    // deployment running zero backends gets no bar at all.
    if (backendHealthContainer_) {
        if (auto* manager =
                app::core::Application::instance().integrationManager()) {
            // Compact strip on Blueprint (top-bar host has no vertical
            // budget for a card); full sidebar card everywhere else.
            // Multi-station reuses the full sidebar layout now too;
            // see chooseMainWindowUI + ADR-0011.
            const auto layout =
                app::config::ConfigManager::instance().getPalette() ==
                        "blueprint"
                    ? app::view::BackendHealthBar::Layout::Compact
                    : app::view::BackendHealthBar::Layout::Sidebar;
            backendHealthBar_ =
                Gtk::make_managed<app::view::BackendHealthBar>(layout);
            backendHealthContainer_->append(*backendHealthBar_);

            backendHealthPresenter_ =
                std::make_unique<app::BackendHealthPresenter>(*manager);
            backendHealthPresenter_->addObserver(this);

            // Synchronous first poll so the bar already has its rows
            // populated by the time the window paints -- otherwise the
            // user sees an empty header for ~1s and then the entries
            // pop in on the first timer tick.
            backendHealthPresenter_->poll();

            // 1 Hz poll. Cheap (atomic loads + small string format)
            // and matches operator perception -- a 200ms latency on a
            // status dot is invisible.
            constexpr unsigned int kBackendPollIntervalMs = 1000;
            backendHealthTimer_ = Glib::signal_timeout().connect(
                [this]() {
                    if (backendHealthPresenter_) {
                        backendHealthPresenter_->poll();
                    }
                    return true;  // re-arm
                },
                /*interval=*/kBackendPollIntervalMs);
        }
    }

    // Live clock.
    if (clockContainer_) {
        clock_ = Gtk::make_managed<app::view::LiveClock>();
        clockContainer_->append(*clock_);
    }
}

void MainWindow::onBackendHealthChanged(
    const app::presenter::BackendHealthViewModel& viewModel) {
    if (backendHealthBar_) backendHealthBar_->update(viewModel);
}

void MainWindow::refreshSidebarTranslations() {
    // main-window.ui is only parsed once at startup, so GtkBuilder's
    // translation machinery doesn't run again after a runtime language
    // switch. Re-assign the strings manually via `_()` -- that hits the
    // freshly re-bound gettext catalog.
    if (appTitleLabel_)    appTitleLabel_->set_label(_("Industrial HMI"));
    if (appSubtitleLabel_) appSubtitleLabel_->set_label(_("Production Monitor"));
    if (closeAppButton_)   closeAppButton_->set_label(_("Close Application"));
    if (versionLabel_)     versionLabel_->set_label(_("Version 1.2.0"));
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
    auto& app    = app::core::Application::instance();
    auto& logger = app.logger();

    // Audit hookup is conditional: when the composition root wired
    // both the audit sink AND the session, every presenter gets a
    // call to setAudit() so its action handlers record rows. With
    // either pointer null the presenters stay free of audit calls
    // (auth-disabled builds, unit tests, etc.).
    auto* audit   = app.auditLogger();
    auto* session = app.authSession();

    // Dashboard -- single station by default, multi-station when the
    // composition root injected a secondary model (set via
    // `Application::setSecondaryProductionModel` when config has
    // `ui.multistation_enabled = true`; see ADR-0011).
    dashboardPresenter_ = std::make_shared<app::DashboardPresenter>();
    dashboardPresenter_->setLogger(logger);
    if (alertCenter_) {
        dashboardPresenter_->setAlertCenter(*alertCenter_);
    }
    if (audit != nullptr && session != nullptr) {
        dashboardPresenter_->setAudit(*audit, *session);
    }

    // Multi-station: when a secondary model is wired in, replace
    // the Dashboard tab with MultiStationDashboardPage hosting two
    // DashboardPage instances. The page uses an aggressively
    // compacted variant (smaller gauges, no trend charts) so the
    // two panes fit alongside the full sidebar without overflow.
    // See ADR-0011.
    auto* secondaryModel = app.secondaryProductionModel();
    if (secondaryModel != nullptr) {
        // Multi-station path: build a SECOND DashboardPresenter for
        // the secondary and host both in a MultiStationDashboardPage.
        // Sidebar (E-STOP, status badge, alerts, I/O) stays bound to
        // the primary presenter -- that's the canonical operator
        // surface; the secondary is a passive secondary monitor here.
        // Secondary presenter intentionally does NOT get the alert center
        // or audit logger: alerts and audit are an operator-level
        // concern handled by the primary.
        secondaryDashboardPresenter_ =
            std::make_shared<app::DashboardPresenter>(*secondaryModel);
        secondaryDashboardPresenter_->setLogger(logger);

        multiStationDashboardPage_ =
            Gtk::make_managed<app::view::MultiStationDashboardPage>(*dialogManager_);
        multiStationDashboardPage_->initialize(dashboardPresenter_,
                                               secondaryDashboardPresenter_);
        registerPage(multiStationDashboardPage_);
    } else {
        dashboardPage_ = Gtk::make_managed<app::view::DashboardPage>(*dialogManager_);
        dashboardPage_->initialize(dashboardPresenter_);
        registerPage(dashboardPage_);
    }

    // Products
    productsPresenter_ = std::make_shared<app::ProductsPresenter>();
    productsPresenter_->setLogger(logger);
    if (audit != nullptr && session != nullptr) {
        productsPresenter_->setAudit(*audit, *session);
    }
    productsPage_ = Gtk::make_managed<app::view::ProductsPage>(*dialogManager_);
    productsPage_->initialize(productsPresenter_);
    registerPage(productsPage_);

    // Role-based UI gating. When a session is wired (auth.enabled),
    // each page's applyRole() trims its control surface to what the
    // active role is allowed to do. Skipped when no session is set
    // so the no-auth dev path keeps the full Admin-equivalent surface.
    if (session != nullptr) {
        const auto userOpt = session->currentUser();
        if (userOpt.has_value()) {
            const auto role = userOpt->role;
            if (dashboardPage_ != nullptr) {
                dashboardPage_->applyRole(role);
            }
            if (multiStationDashboardPage_ != nullptr) {
                multiStationDashboardPage_->applyRole(role);
            }
            productsPage_->applyRole(role);
        }
    }

    // Settings (no presenter -- pure view over ConfigManager/ThemeManager/Logger)
    settingsPage_ = Gtk::make_managed<app::view::SettingsPage>(*dialogManager_);
    registerPage(settingsPage_);

    // History page: only mounted when the historian's read interface
    // is wired in. main() builds the SQLite store on `historian.enabled
    // == true` AND a successful `initialize()` -- a failed open leaves
    // the pointer null and we skip the tab silently (no useful chart
    // without data, no point misleading the operator).
    if (auto* reader =
            app::core::Application::instance().historyReader()) {
        historyPage_ = Gtk::make_managed<app::view::HistoryPage>(
            *dialogManager_, *reader);
        registerPage(historyPage_);
    }

    // Audit log page: admin-only. The page exists in the binary
    // regardless of role, but only mounted in the notebook when the
    // current session holds Admin. Operator / Maintenance never
    // see the tab at all -- defence in depth beyond the role check
    // a future user-management surface will need too.
    if (auto* audit = app.auditLogger();
        audit != nullptr && session != nullptr) {
        const auto userOpt = session->currentUser();
        if (userOpt.has_value()
                && app::auth::canViewAuditLog(userOpt->role)) {
            auditLogPage_ = Gtk::make_managed<app::view::AuditLogPage>(
                *dialogManager_, *audit);
            registerPage(auditLogPage_);
        }
    }

    // Users management page: admin-only. Same gating story as the
    // audit page above -- the presenter's list() also returns empty
    // for non-admins, so even a forced registration would render
    // blank, but skipping the tab outright is cleaner UX.
    if (auto* users = app.usersPresenter();
        users != nullptr && session != nullptr) {
        const auto userOpt = session->currentUser();
        if (userOpt.has_value()
                && app::auth::canManageUsers(userOpt->role)) {
            usersPage_ = Gtk::make_managed<app::view::UsersPage>(
                *dialogManager_, *users);
            registerPage(usersPage_);
        }
    }

#ifdef INDUSTRIAL_HMI_HAS_ML_PLUGIN
    // Edge AI inspection page. Only registered when both the ONNX
    // model and the ImageNet labels file are on disk. Missing either
    // is a soft skip -- the rest of the UI keeps working without the
    // tab, and a log line points at the Python pipeline.
    {
        const std::filesystem::path modelPath(
            "assets/models/mobilenetv2_int8.onnx");
        const std::filesystem::path labelsPath(
            "assets/models/imagenet_labels.txt");

        if (!std::filesystem::exists(modelPath) ||
            !std::filesystem::exists(labelsPath)) {
            logger.warn(
                "Edge AI: skipping Inspection tab -- model or labels "
                "missing under assets/models/. Run scripts/ml/quantize_model.py "
                "and scripts/ml/export_labels.py to generate them.");
        } else {
            try {
                inspectionDecoder_ =
                    std::make_unique<app::ml::ImageDecoder>();
                // Plugin-loading classifier; ORT shared library is NOT
                // pulled into industrial-hmi.exe at boot. The dlopen
                // happens here on first construction.
                inspectionClassifier_ =
                    std::make_unique<app::ml::OnnxImageClassifier>(
                        modelPath, labelsPath);
                inspectionPresenter_ = std::make_shared<
                    app::presenter::QualityInspectionPresenter>(
                        *inspectionClassifier_, *inspectionDecoder_);
                inspectionPresenter_->setLogger(logger);

                inspectionPage_ = Gtk::make_managed<
                    app::view::QualityInspectionPage>(*dialogManager_);
                inspectionPage_->initialize(inspectionPresenter_);
                inspectionPresenter_->initialize();
                registerPage(inspectionPage_);
                logger.info(
                    "Edge AI: Inspection tab registered (model={})",
                    modelPath.string());
            } catch (const std::exception& exc) {
                logger.error(
                    "Edge AI: failed to wire Inspection tab: {}",
                    exc.what());
            }
        }
    }
#endif

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
    dashboardPage_             = nullptr;
    multiStationDashboardPage_ = nullptr;
    productsPage_              = nullptr;
    settingsPage_              = nullptr;
#ifdef INDUSTRIAL_HMI_HAS_ML_PLUGIN
    // mainNotebook_->remove_page above destroyed QualityInspectionPage;
    // its destructor unregistered itself from the presenter, so the
    // tear-down below is safe.
    inspectionPage_ = nullptr;
    inspectionPresenter_.reset();
    inspectionClassifier_.reset();
    inspectionDecoder_.reset();
#endif
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
                    // Palettes that ship only one mode by design --
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
    secondaryDashboardPresenter_.reset();
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

    // 8) Re-translate sidebar widgets (branding + Close Application) --
    //    they were loaded by GtkBuilder at startup, not touched by the
    //    page rebuild above.
    refreshSidebarTranslations();

    //    Also re-translate retained alert content (history rows
    //    especially -- active alerts will re-raise on the next model
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

void MainWindow::handleSignOut() {
    // Hide the main window, run the login dialog modally, then
    // either spawn a fresh MainWindow (operator signed back in --
    // common path for shift changes) or quit the application
    // (operator cancelled -- they're done with the terminal).
    //
    // Earlier iterations just called Application::quit() and relied
    // on the docker container's `restart: unless-stopped` policy to
    // spin a fresh process back to the login dialog. That worked in
    // Docker but on native Windows / Linux there's no restart
    // supervisor, so sign-out felt like a crash. The rebuild pattern
    // works the same on every host AND re-evaluates role-gated
    // page registration so admin <-> operator swaps see the right
    // tab list.
    set_visible(false);

    auto* svc     = app::core::Application::instance().authService();
    auto* session = app::core::Application::instance().authSession();
    // get_default() returns Gio::Application; cast to Gtk::Application
    // so add_window / remove_window are accessible.
    auto gtkApp = std::dynamic_pointer_cast<Gtk::Application>(
        Gtk::Application::get_default());
    if (svc == nullptr || session == nullptr || !gtkApp) {
        // Defensive: no auth wiring or no app to attach the dialog
        // to. Just close the window so main() unwinds.
        close();
        return;
    }

    app::view::LoginDialog dialog(*svc, *session);
    gtkApp->add_window(dialog);
    const auto outcome = dialog.runModal();
    gtkApp->remove_window(dialog);

    if (outcome != app::view::LoginDialog::Result::Success) {
        // Application::quit() returns from run() so main() shuts
        // down cleanly.
        gtkApp->quit();
        return;
    }

    // Re-login succeeded. Rebuild MainWindow from scratch so role-
    // gated pages (UsersPage, AuditLogPage) re-evaluate visibility
    // against the NEW currentUser. The auth + model + historian
    // singletons live on main()'s stack and survive the swap; only
    // per-window pages + presenters recycle.
    //
    // Three tricky bits:
    //
    //  1. Application keep-alive. The current window is THIS app's
    //     only toplevel; closing it sends the window count to 0 and
    //     GApplication starts its shutdown sequence. By the time the
    //     idle callback below fires, add_window() rejects the fresh
    //     window with a "must be added after startup" critical, the
    //     window never materialises, and the process exits. hold()
    //     pins an extra reference so the app survives the gap;
    //     release() in the idle callback balances it once the new
    //     window has taken over.
    //
    //  2. Tear down BEFORE re-build. The old MainWindow's
    //     presenters subscribe to the SimulatedModel singleton; if
    //     a fresh MainWindow is constructed while the old one still
    //     lives, BOTH presenters fire on every tick and every event
    //     shows up twice. Calling `delete this` here is unsafe
    //     (we're in our own method), so we close() now (queues the
    //     gtkmm destruction) and let the idle callback fire AFTER
    //     gtk has actually destroyed the window. The single idle
    //     iteration is enough -- GTK reaps the closed window before
    //     the next main loop tick.
    //
    //  3. Defer to idle (not direct) so we unwind THIS MainWindow's
    //     call stack (we're inside one of its lambdas) before its
    //     destructor runs. Calling new MainWindow inline would
    //     re-enter widget code while our `this` is still alive.
    gtkApp->hold();
    Glib::signal_idle().connect_once([gtkApp]() {
        auto* fresh = new MainWindow();
        gtkApp->add_window(*fresh);
        // Drain pending idle callbacks from the fresh window's
        // initial paint (mirrors what Application::run does on
        // first activate).
        while (g_main_context_iteration(nullptr, false)) {}
        fresh->present();
        gtkApp->release();
    });
    close();
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
    secondaryDashboardPresenter_.reset();
    productsPresenter_.reset();

    // 5) Parse the new .ui into a DETACHED subtree (no set_child yet).
    //    Member widget pointers -- mainNotebook_, alertsContainer_, etc.
    //    -- now point into the freshly-built, not-yet-installed tree.
    //    The raw Page/alerts/clock pointers still reference widgets in
    //    the currently-displayed old tree, so we zero them out: the
    //    corresponding widgets will be destroyed when set_child swaps
    //    the roots below.
    pages_.clear();
    dashboardPage_             = nullptr;
    multiStationDashboardPage_ = nullptr;
    productsPage_              = nullptr;
    settingsPage_              = nullptr;
    alertsPanel_               = nullptr;
    statusBadge_               = nullptr;
    clock_                     = nullptr;
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

    // 7) Atomic swap -- GTK destroys the old root (and everything under
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
