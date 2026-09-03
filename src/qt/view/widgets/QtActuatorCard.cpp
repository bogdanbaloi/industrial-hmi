#include "src/qt/view/widgets/QtActuatorCard.h"

#include <QLabel>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>

namespace app::view {

namespace {

const char* statusText(presenter::ActuatorCardStatus status) {
    using Status = presenter::ActuatorCardStatus;
    switch (status) {
        case Status::Offline:     return "Offline";
        case Status::Idle:        return "Idle";
        case Status::Working:     return "Working";
        case Status::Error:       return "Error";
        case Status::Homing:      return "Homing";
        case Status::Calibrating: return "Calibrating";
        case Status::Unknown:     return "Unknown";
    }
    return "Unknown";
}

const char* statusColor(presenter::ActuatorCardStatus status) {
    using Status = presenter::ActuatorCardStatus;
    switch (status) {
        case Status::Error:   return "#c62828";  // red
        case Status::Offline:
        case Status::Unknown: return "#616161";  // grey
        default:              return "#2e7d32";  // green
    }
}

}  // namespace

QtActuatorCard::QtActuatorCard(std::uint32_t actuatorId, QWidget* parent)
    : QGroupBox(parent), actuatorId_(actuatorId) {
    setTitle(QString("Actuator %1").arg(actuatorId_));

    auto* layout = new QVBoxLayout(this);

    statusLabel_  = new QLabel("Status: -", this);
    messageLabel_ = new QLabel("-", this);
    flagsLabel_   = new QLabel("-", this);

    layout->addWidget(statusLabel_);
    layout->addWidget(messageLabel_);
    layout->addWidget(flagsLabel_);
}

void QtActuatorCard::applyViewModel(
    const presenter::ActuatorCardViewModel& viewModel) {
    statusLabel_->setText(
        QString("Status: %1").arg(statusText(viewModel.status)));
    statusLabel_->setStyleSheet(
        QString("color: %1; font-weight: bold;").arg(statusColor(viewModel.status)));

    messageLabel_->setText(QString::fromStdString(viewModel.statusMessage));

    // Compact flag line: mode, home and alert state at a glance.
    QStringList flags;
    flags << (viewModel.autoMode ? "AUTO" : "MANUAL");
    if (viewModel.atHomePosition) {
        flags << "HOME";
    }
    if (viewModel.hasAlert) {
        flags << "ALERT";
    }
    flagsLabel_->setText(flags.join(" | "));
}

}  // namespace app::view
