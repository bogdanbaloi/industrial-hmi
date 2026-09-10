#pragma once

#include <QWidget>

#include <sigc++/connection.h>

#include <memory>

class QEvent;

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtAlertsPage;
}

namespace app::presenter {
class AlertCenter;
struct AlertViewModel;
}

namespace app::view {

/// Operator alert panel for the Qt shell. Subscribes to the shared AlertCenter
/// (the same ISA-18.2 alarm store the GTK AlertsPanel renders) and rebuilds its
/// card list on every lifecycle change. Reusing AlertCenter unchanged behind a
/// second toolkit is part of the toolkit-independence proof (REQ-ARCH-011): the
/// alarm store, like the presenters, never learns whether GTK or Qt draws it.
///
/// Threading: the Qt simulation tick runs on the UI thread, so AlertCenter's
/// change signals fire on the UI thread and the rebuild touches widgets
/// directly, with no marshalling (mirrors QtDashboardPage).
class QtAlertsPage : public QWidget {
public:
    explicit QtAlertsPage(presenter::AlertCenter& alertCenter,
                          QWidget* parent = nullptr);
    ~QtAlertsPage() override;

    QtAlertsPage(const QtAlertsPage&)            = delete;
    QtAlertsPage& operator=(const QtAlertsPage&) = delete;
    QtAlertsPage(QtAlertsPage&&)                 = delete;
    QtAlertsPage& operator=(QtAlertsPage&&)      = delete;

protected:
    void changeEvent(QEvent* event) override;

private:
    void scheduleRebuild();
    void rebuild();
    QWidget* buildCard(const presenter::AlertViewModel& alert, bool historyMode,
                       const QString& resolvedAt);

    presenter::AlertCenter&           alertCenter_;
    std::unique_ptr<Ui::QtAlertsPage> ui_;
    sigc::connection                  alertsConn_;
    sigc::connection                  historyConn_;
    bool                              showingHistory_{false};
};

}  // namespace app::view
