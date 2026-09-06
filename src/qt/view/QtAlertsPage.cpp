// Presenter headers first: AlertViewModel / AlertCenter define no ERROR
// enumerator, but we keep the model-before-Qt include order the rest of the Qt
// frontend uses so the wingdi.h ERROR macro can never surprise a future edit.
#include "src/presenter/AlertCenter.h"
#include "src/presenter/modelview/AlertViewModel.h"

#include "src/qt/view/QtAlertsPage.h"

#include "src/qt/view/QtTheme.h"

#include "ui_QtAlertsPage.h"

#include <sigc++/functors/mem_fun.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

#include <string>

namespace app::view {

namespace {

constexpr int kCardSpacing        = 2;
constexpr int kCardHeaderSpacing  = 6;
constexpr int kStateBadgeMinWidth = 52;

// Map an alert severity to the shared theme status colour (one source of truth
// in QtTheme.h -- no widget hardcodes a hex string).
const char* severityColor(presenter::AlertSeverity severity) {
    switch (severity) {
        case presenter::AlertSeverity::Info:     return theme::kColorInfo;
        case presenter::AlertSeverity::Warning:  return theme::kColorWarning;
        case presenter::AlertSeverity::Critical: return theme::kColorAlarm;
    }
    return theme::kColorNeutral;
}

// Short ISA-18.2 lifecycle badge (UNACK / ACK / RTN / SHELVED).
QString stateBadge(presenter::AlarmState state) {
    switch (state) {
        case presenter::AlarmState::UnackActive: return QObject::tr("UNACK");
        case presenter::AlarmState::AckActive:   return QObject::tr("ACK");
        case presenter::AlarmState::RtnUnack:    return QObject::tr("RTN");
        case presenter::AlarmState::Shelved:     return QObject::tr("SHELVED");
    }
    return {};
}

}  // namespace

QtAlertsPage::QtAlertsPage(presenter::AlertCenter& alertCenter, QWidget* parent)
    : QWidget(parent),
      alertCenter_(alertCenter),
      ui_(std::make_unique<Ui::QtAlertsPage>()) {
    ui_->setupUi(this);

    // Toggle between the active alarm list and the resolved-alarm history.
    connect(ui_->historyButton, &QPushButton::toggled, this, [this](bool on) {
        showingHistory_ = on;
        ui_->headerLabel->setText(on ? tr("History") : tr("Alerts"));
        ui_->clearButton->setText(on ? tr("Clear history") : tr("Clear all"));
        rebuild();
    });
    // The action button clears whichever list is currently shown.
    connect(ui_->clearButton, &QPushButton::clicked, this, [this] {
        if (showingHistory_) {
            alertCenter_.clearHistory();
        } else {
            alertCenter_.clearAll();
        }
    });

    // Any lifecycle change (raise / ack / clear / resolve) rebuilds the list.
    // AlertCenter emits on the mutating thread; here that is always the UI
    // thread, so the rebuild can touch widgets without marshalling. mem_fun
    // (not a lambda) matches the GTK AlertsPanel and keeps the slot free of a
    // captured `this` with static storage duration; we disconnect in the dtor.
    alertsConn_ = alertCenter_.signalAlertsChanged().connect(
        sigc::mem_fun(*this, &QtAlertsPage::rebuild));
    historyConn_ = alertCenter_.signalHistoryChanged().connect(
        sigc::mem_fun(*this, &QtAlertsPage::rebuild));

    rebuild();
}

QtAlertsPage::~QtAlertsPage() {
    alertsConn_.disconnect();
    historyConn_.disconnect();
}

void QtAlertsPage::rebuild() {
    // Wipe the current cards (the list is tiny, a full rebuild is simplest and
    // matches the GTK AlertsPanel) then repopulate from the active / history
    // snapshot.
    while (QLayoutItem* item = ui_->alertsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    if (showingHistory_) {
        const auto entries = alertCenter_.history();
        if (entries.empty()) {
            ui_->alertsLayout->addWidget(new QLabel(tr("No history")));
        } else {
            for (const auto& entry : entries) {
                ui_->alertsLayout->addWidget(
                    buildCard(entry.alert, /*historyMode=*/true,
                              QString::fromStdString(entry.resolvedAt)));
            }
        }
    } else {
        const auto active = alertCenter_.snapshot();
        if (active.empty()) {
            ui_->alertsLayout->addWidget(new QLabel(tr("No alerts")));
        } else {
            for (const auto& alert : active) {
                ui_->alertsLayout->addWidget(
                    buildCard(alert, /*historyMode=*/false, QString()));
            }
        }
    }
    ui_->alertsLayout->addStretch();
}

QWidget* QtAlertsPage::buildCard(const presenter::AlertViewModel& alert,
                                 bool historyMode, const QString& resolvedAt) {
    auto* card = new QFrame();
    card->setFrameShape(QFrame::StyledPanel);
    auto* layout = new QVBoxLayout(card);
    layout->setSpacing(kCardSpacing);

    auto* top = new QHBoxLayout();
    top->setSpacing(kCardHeaderSpacing);

    // Title carries the severity colour so a red/amber/blue scan works before
    // reading the text.
    auto* title = new QLabel(QString::fromStdString(alert.title));
    title->setStyleSheet(theme::coloredBold(severityColor(alert.severity)));
    title->setWordWrap(true);
    top->addWidget(title, 1);

    // ISA-18.2 priority badge (P1..P4): urgency at a glance, colour-blind safe.
    top->addWidget(new QLabel(QStringLiteral("P%1").arg(alert.priority)));

    // Lifecycle badge only in the active view; history rows are all resolved.
    if (!historyMode) {
        auto* badge = new QLabel(stateBadge(alert.state));
        badge->setMinimumWidth(kStateBadgeMinWidth);
        top->addWidget(badge);
    }

    // Active rows show when the alarm was raised; history rows when resolved.
    top->addWidget(new QLabel(
        historyMode ? resolvedAt : QString::fromStdString(alert.timestamp)));

    // Per-alarm Acknowledge (ISA-18.2): an unacknowledged active alarm becomes
    // acknowledged but stays visible until its condition clears; one that has
    // returned to normal is fully resolved. The operator cannot make a
    // transient fault vanish unseen.
    if (!historyMode) {
        auto* ack = new QPushButton(tr("Acknowledge"));
        const std::string key = alert.key;
        connect(ack, &QPushButton::clicked, this,
                [this, key] { alertCenter_.acknowledge(key); });
        top->addWidget(ack);
    }

    layout->addLayout(top);

    if (!alert.message.empty()) {
        auto* message = new QLabel(QString::fromStdString(alert.message));
        message->setWordWrap(true);
        layout->addWidget(message);
    }

    return card;
}

}  // namespace app::view
