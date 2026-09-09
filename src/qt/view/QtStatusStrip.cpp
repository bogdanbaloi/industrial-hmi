// View-model header first so BackendState is parsed before any Qt header pulls
// in wingdi.h (ERROR=0 macro).
#include "src/presenter/modelview/BackendHealthViewModel.h"

#include "src/qt/view/QtStatusStrip.h"

#include "src/qt/view/QtTheme.h"
#include "src/qt/view/QtUiDispatch.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QObject>
#include <QString>
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
        case kStateRunning:
            return {.text = QObject::tr("Running"), .color = theme::kColorOk};
        case kStateError:
            return {.text = QObject::tr("Error"), .color = theme::kColorAlarm};
        case kStateCalibration:
            return {.text  = QObject::tr("Calibration"),
                    .color = theme::kColorInfo};
        case kStateIdle:
        default:
            return {.text  = QObject::tr("Idle"),
                    .color = theme::kColorNeutral};
    }
}

Style backendStyle(integration::BackendState state) {
    using State = integration::BackendState;
    switch (state) {
        case State::Connected:
            return {.text = QObject::tr("online"), .color = theme::kColorOk};
        case State::Connecting:
            return {.text  = QObject::tr("connecting"),
                    .color = theme::kColorWarning};
        case State::Degraded:
            return {.text  = QObject::tr("degraded"),
                    .color = theme::kColorWarning};
        case State::Disconnected:
            return {.text  = QObject::tr("offline"),
                    .color = theme::kColorNeutral};
    }
    return {.text = QObject::tr("offline"), .color = theme::kColorNeutral};
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

    // Connectivity (state pill + backend chips) sits on the left; the summary +
    // clock sit on the right, with the slack between the two groups.
    layout->addStretch();
    summaryLabel_ = new QLabel();
    summaryLabel_->setStyleSheet(theme::coloredBold(theme::kColorNeutral));
    layout->addWidget(summaryLabel_);
    layout->addSpacing(kBarSpacing);

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

    int online = 0;
    for (const auto& entry : viewModel.entries) {
        if (entry.state == integration::BackendState::Connected) {
            ++online;
        }
    }
    const auto total = static_cast<int>(viewModel.entries.size());
    summaryLabel_->setText(
        total == 0 ? tr("No backends configured")
                   : tr("%1 of %2 backends online").arg(online).arg(total));

    for (const auto& entry : viewModel.entries) {
        const Style style = backendStyle(entry.state);
        // Each backend is a bordered chip carrying its name in its state colour
        // (green online / amber connecting / grey offline), with the state word
        // + metrics in the tooltip. Clearer than a run of coloured bullets.
        auto* chip = new QLabel(QString::fromStdString(entry.name));
        chip->setStyleSheet(
            QString("border: 1px solid %1; color: %1; border-radius: 8px;"
                    " padding: 1px 8px; font-weight: bold;")
                .arg(style.color));
        QString tip = style.text;
        if (!entry.metricsLine.empty()) {
            tip += QStringLiteral(" · ") +
                   QString::fromStdString(entry.metricsLine);
        }
        chip->setToolTip(tip);
        backendsLayout_->addWidget(chip);
    }
}

void QtStatusStrip::updateClock() {
    clock_->setText(QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd  HH:mm:ss")));
}

}  // namespace app::view
