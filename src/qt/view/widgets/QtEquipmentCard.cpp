#include "src/qt/view/widgets/QtEquipmentCard.h"

#include <QCheckBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QString>
#include <QVBoxLayout>

#include <utility>

namespace app::view {

namespace {

const char* statusText(presenter::EquipmentCardStatus status) {
    using Status = presenter::EquipmentCardStatus;
    switch (status) {
        case Status::Offline:     return "Offline";
        case Status::StartingUp:  return "Starting up";
        case Status::CheckOutput: return "Self-check";
        case Status::Online:      return "Online";
        case Status::Processing:  return "Processing";
        case Status::WarmingUp:   return "Warming up";
        case Status::Ready:       return "Ready";
        case Status::Reboot:      return "Reboot";
        case Status::Error:       return "Error";
        case Status::Disabled:    return "Disabled";
        case Status::Unknown:     return "Unknown";
    }
    return "Unknown";
}

const char* statusColor(presenter::EquipmentCardStatus status) {
    using Status = presenter::EquipmentCardStatus;
    switch (status) {
        case Status::Error:    return "#c62828";  // red
        case Status::Offline:
        case Status::Disabled:
        case Status::Unknown:  return "#616161";  // grey
        default:               return "#2e7d32";  // green
    }
}

}  // namespace

QtEquipmentCard::QtEquipmentCard(std::uint32_t equipmentId,
                                 ToggleCallback onToggle, QWidget* parent)
    : QGroupBox(parent),
      equipmentId_(equipmentId),
      onToggle_(std::move(onToggle)) {
    setTitle(QString("Equipment %1").arg(equipmentId_));

    auto* layout = new QVBoxLayout(this);

    statusLabel_      = new QLabel("Status: -", this);
    consumablesLabel_ = new QLabel("Supplies: -", this);
    messageLabel_     = new QLabel("-", this);
    enableCheck_      = new QCheckBox("Enabled", this);

    layout->addWidget(statusLabel_);
    layout->addWidget(consumablesLabel_);
    layout->addWidget(messageLabel_);
    layout->addWidget(enableCheck_);

    // Back-channel: an operator toggle forwards to the presenter through the
    // callback. Programmatic updates from applyViewModel are done under a
    // QSignalBlocker so they never re-enter this path.
    connect(enableCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (onToggle_) {
            onToggle_(equipmentId_, checked);
        }
    });
}

void QtEquipmentCard::applyViewModel(
    const presenter::EquipmentCardViewModel& viewModel) {
    statusLabel_->setText(
        QString("Status: %1").arg(statusText(viewModel.status)));
    statusLabel_->setStyleSheet(
        QString("color: %1; font-weight: bold;").arg(statusColor(viewModel.status)));

    consumablesLabel_->setText(
        QString("Supplies: %1").arg(QString::fromStdString(viewModel.consumables)));
    messageLabel_->setText(QString::fromStdString(viewModel.messageStatus));

    // Reflect the enabled flag without re-triggering the toggled signal.
    const QSignalBlocker blocker(enableCheck_);
    enableCheck_->setChecked(viewModel.enabled);
}

}  // namespace app::view
