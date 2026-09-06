// View-model header first so BackendState is parsed before any Qt header pulls
// in wingdi.h (ERROR=0 macro).
#include "src/presenter/modelview/BackendHealthViewModel.h"

#include "src/qt/view/QtStatusStrip.h"

#include "src/qt/view/QtTheme.h"
#include "src/qt/view/QtUiDispatch.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QObject>
#include <QString>
#include <QTime>
#include <QTimer>

namespace app::view {

namespace {

constexpr int kBarHMargin      = 12;
constexpr int kBarVMargin      = 4;
constexpr int kBarSpacing      = 14;
constexpr int kDotSpacing      = 12;
constexpr int kBadgeRadius     = 9;
constexpr int kClockIntervalMs = 1000;

// System-state (int) codes, mirroring model::SystemState without naming the
// enum (its ERROR member clashes with the wingdi macro).
constexpr int kStateIdle        = 0;
constexpr int kStateRunning     = 1;
constexpr int kStateError       = 2;
constexpr int kStateCalibration = 3;

struct Style {
    QString     text;
    const char* color;
};

Style systemStyle(int state) {
    switch (state) {
        case kStateRunning:     return {QObject::tr("Running"), theme::kColorOk};
        case kStateError:       return {QObject::tr("Error"), theme::kColorAlarm};
        case kStateCalibration: return {QObject::tr("Calibration"),
                                        theme::kColorInfo};
        case kStateIdle:
        default:                return {QObject::tr("Idle"),
                                        theme::kColorNeutral};
    }
}

Style backendStyle(integration::BackendState state) {
    using State = integration::BackendState;
    switch (state) {
        case State::Connected:    return {QObject::tr("online"), theme::kColorOk};
        case State::Connecting:   return {QObject::tr("connecting"),
                                          theme::kColorWarning};
        case State::Degraded:     return {QObject::tr("degraded"),
                                          theme::kColorWarning};
        case State::Disconnected: return {QObject::tr("offline"),
                                          theme::kColorNeutral};
    }
    return {QObject::tr("offline"), theme::kColorNeutral};
}

}  // namespace

QtStatusStrip::QtStatusStrip(QWidget* parent) : QWidget(parent) {
    setObjectName("statusStrip");

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(kBarHMargin, kBarVMargin, kBarHMargin,
                               kBarVMargin);
    layout->setSpacing(kBarSpacing);

    stateBadge_ = new QLabel();
    layout->addWidget(stateBadge_);

    auto* backends = new QWidget();
    backendsLayout_ = new QHBoxLayout(backends);
    backendsLayout_->setContentsMargins(0, 0, 0, 0);
    backendsLayout_->setSpacing(kDotSpacing);
    layout->addWidget(backends);

    layout->addStretch();

    clock_ = new QLabel();
    clock_->setStyleSheet(theme::coloredBold(theme::kColorNeutral));
    layout->addWidget(clock_);

    applySystemState(kStateIdle);

    // Self-contained live clock, the Qt analog of the GTK LiveClock widget.
    clockTimer_ = new QTimer(this);
    connect(clockTimer_, &QTimer::timeout, this, [this] { updateClock(); });
    clockTimer_->start(kClockIntervalMs);
    updateClock();
}

QtStatusStrip::~QtStatusStrip() = default;

void QtStatusStrip::setSystemState(int state) {
    // The presenter's state signal can fire on a backend thread; hop to the UI
    // thread before touching the badge.
    postToUi(this, [this, state] { applySystemState(state); });
}

void QtStatusStrip::applySystemState(int state) {
    const Style style = systemStyle(state);
    stateBadge_->setText(style.text);
    stateBadge_->setStyleSheet(
        QString("background: %1; color: white; border-radius: %2px;"
                " padding: 2px 10px; font-weight: bold;")
            .arg(style.color)
            .arg(kBadgeRadius));
}

void QtStatusStrip::onBackendHealthChanged(
    const presenter::BackendHealthViewModel& viewModel) {
    postToUi(this, [this, viewModel] { applyBackendHealth(viewModel); });
}

void QtStatusStrip::applyBackendHealth(
    const presenter::BackendHealthViewModel& viewModel) {
    // Compact one-line inventory: a coloured "name" per backend, tooltip
    // carrying the state word + metrics. Full rebuild (tiny list).
    while (QLayoutItem* item = backendsLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    for (const auto& entry : viewModel.entries) {
        const Style style = backendStyle(entry.state);
        auto* dot = new QLabel(QStringLiteral("● ") +
                               QString::fromStdString(entry.name));
        dot->setStyleSheet(theme::coloredBold(style.color));
        QString tip = style.text;
        if (!entry.metricsLine.empty()) {
            tip += QStringLiteral(" · ") +
                   QString::fromStdString(entry.metricsLine);
        }
        dot->setToolTip(tip);
        backendsLayout_->addWidget(dot);
    }
}

void QtStatusStrip::updateClock() {
    clock_->setText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
}

}  // namespace app::view
