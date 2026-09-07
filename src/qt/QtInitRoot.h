#pragma once

#include <sigc++/connection.h>

#include <chrono>
#include <memory>
#include <string>

class QTimer;

namespace app::core {
class Bootstrap;
}

namespace app {
class DashboardPresenter;
class ProductsPresenter;
class BackendHealthPresenter;
}

namespace app::presenter {
class AlertCenter;
class QualityInspectionPresenter;
}

namespace app::ml {
class ImageDecoder;
class FakeImageClassifier;
}

namespace app::integration {
struct IntegrationServices;
}

namespace app::historian {
class SqliteHistoryStore;
class HistorianBridge;
class HistorianMaintenance;
}

namespace app::view {
class QtMainWindow;
class QtPaletteManager;
class QtGettextTranslator;
}

namespace app::qt {

/// Composition root for the Qt desktop frontend. The direct counterpart to
/// `app::console::InitConsole`: it builds the same Model + Presenter + View
/// collaborators, wires them, drives the simulation tick, and shows the shell
/// window. The existence of a third composition root over the SAME presenter
/// and model layer is the concrete proof that the MVP View seam is
/// toolkit-independent (ADR-0020, extends ADR-0002).
///
/// The tick runs on a UI-thread QTimer, so presenter callbacks reach the
/// widgets on the UI thread without cross-thread marshalling.
///
/// Ownership: borrows the already-prepared Bootstrap (logger + config).
/// Bootstrap must out-live this object; in practice both are stack objects in
/// `main()`.
class QtInitRoot {
public:
    explicit QtInitRoot(core::Bootstrap& bootstrap);
    ~QtInitRoot();

    QtInitRoot(const QtInitRoot&)            = delete;
    QtInitRoot& operator=(const QtInitRoot&) = delete;
    QtInitRoot(QtInitRoot&&)                 = delete;
    QtInitRoot& operator=(QtInitRoot&&)      = delete;

    /// Build the presenter + shell, attach the observer, start the tick timer
    /// and show the window. Non-blocking: the Qt event loop is run by
    /// `QApplication::exec()` back in `main()`.
    void run();

private:
    /// Push the active-alarm count onto the sidebar Alerts badge, marshalled to
    /// the UI thread (the AlertCenter signal may fire on a backend thread).
    void refreshAlertsBadge();

    /// Build the historian stack (SQLite store + model bridge + retention
    /// worker) when enabled in config. Degraded-open policy: a store that fails
    /// to open is dropped and the History page never mounts, matching the GTK
    /// frontend and main()'s registerHistorian.
    void buildHistorian();

    /// Apply a language selection from the Settings picker: persist it, rebind
    /// the shared gettext catalog, then reinstall the translator so Qt
    /// broadcasts QEvent::LanguageChange and every widget retranslates live.
    void changeLanguage(const std::string& code);

    core::Bootstrap&                    bootstrap_;
    std::unique_ptr<integration::IntegrationServices> integrationServices_;
    std::unique_ptr<presenter::AlertCenter>           alertCenter_;
    std::unique_ptr<BackendHealthPresenter>           backendHealthPresenter_;
    std::unique_ptr<DashboardPresenter>     dashboardPresenter_;
    std::unique_ptr<ProductsPresenter>      productsPresenter_;
    std::unique_ptr<ml::ImageDecoder>            imageDecoder_;
    std::unique_ptr<ml::FakeImageClassifier>     imageClassifier_;
    std::unique_ptr<presenter::QualityInspectionPresenter> inspectionPresenter_;
    std::unique_ptr<historian::SqliteHistoryStore>   historyStore_;
    std::unique_ptr<historian::HistorianBridge>      historianBridge_;
    std::unique_ptr<historian::HistorianMaintenance> historianMaintenance_;
    std::unique_ptr<view::QtGettextTranslator> translator_;
    std::unique_ptr<view::QtPaletteManager> paletteManager_;
    std::unique_ptr<view::QtMainWindow>     window_;
    std::unique_ptr<QTimer>                 tickTimer_;
    sigc::connection                        systemStateConn_;
    sigc::connection                        dashboardStateConn_;
    sigc::connection                        alertsBadgeConn_;

    static constexpr std::chrono::milliseconds kTickPeriod{2000};
};

}  // namespace app::qt
