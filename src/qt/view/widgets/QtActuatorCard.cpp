#include "src/qt/view/widgets/QtActuatorCard.h"

#include "src/qt/view/QtTheme.h"

#include <QEvent>
#include <QLabel>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>

namespace app::view {

namespace {

QString statusText(presenter::ActuatorCardStatus status) {
    using Status = presenter::ActuatorCardStatus;
    switch (status) {
        case Status::Offline:     return QObject::tr("Offline");
        case Status::Idle:        return QObject::tr("Idle");
        case Status::Working:     return QObject::tr("Working");
        case Status::Error:       return QObject::tr("Error");
        case Status::Homing:      return QObject::tr("Homing");
        case Status::Calibrating: return QObject::tr("Calibrating");
        case Status::Unknown:     return QObject::tr("Unknown");
    }
    return QObject::tr("Unknown");
}

const char* statusColor(presenter::ActuatorCardStatus status) {
    using Status = presenter::ActuatorCardStatus;
    switch (status) {
        case Status::Error:
            return theme::kColorAlarm;
        case Status::Offline:
        case Status::Unknown:
            return theme::kColorNeutral;
        default:
            return theme::kColorOk;
    }
}

}  // namespace

QtActuatorCard::QtActuatorCard(std::uint32_t actuatorId, QWidget* parent)
    : QGroupBox(parent), actuatorId_(actuatorId) {
    setTitle(tr("Actuator %1").arg(actuatorId_));

    auto* layout = new QVBoxLayout(this);

    statusLabel_  = new QLabel(tr("Status: -"), this);
    messageLabel_ = new QLabel("-", this);
    flagsLabel_   = new QLabel("-", this);

    layout->addWidget(statusLabel_);
    layout->addWidget(messageLabel_);
    layout->addWidget(flagsLabel_);
}

void QtActuatorCard::applyViewModel(
    const presenter::ActuatorCardViewModel& viewModel) {
    lastViewModel_ = viewModel;
    statusLabel_->setText(tr("Status: %1").arg(statusText(viewModel.status)));
    statusLabel_->setStyleSheet(theme::coloredBold(statusColor(viewModel.status)));

    messageLabel_->setText(QString::fromStdString(viewModel.statusMessage));

    // Compact flag line: mode, home and alert state at a glance.
    QStringList flags;
    flags << (viewModel.autoMode ? tr("AUTO") : tr("MANUAL"));
    if (viewModel.atHomePosition) {
        flags << tr("HOME");
    }
    if (viewModel.hasAlert) {
        flags << tr("ALERT");
    }
    flagsLabel_->setText(flags.join(" | "));
}

void QtActuatorCard::changeEvent(QEvent* event) {
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        setTitle(tr("Actuator %1").arg(actuatorId_));
        if (lastViewModel_) {
            applyViewModel(*lastViewModel_);
        } else {
            statusLabel_->setText(tr("Status: -"));
        }
    }
    QGroupBox::changeEvent(event);
}

}  // namespace app::view
