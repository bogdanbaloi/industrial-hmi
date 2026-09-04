#include "src/qt/view/widgets/QtEquipmentCard.h"

#include "src/qt/view/QtTheme.h"

#include <QCheckBox>
#include <QLabel>
#include <QObject>
#include <QSignalBlocker>
#include <QString>
#include <QVBoxLayout>

#include <utility>

namespace app::view {

namespace {

QString statusText(presenter::EquipmentCardStatus status) {
    using Status = presenter::EquipmentCardStatus;
    switch (status) {
        case Status::Offline:     return QObject::tr("Offline");
        case Status::StartingUp:  return QObject::tr("Starting up");
        case Status::CheckOutput: return QObject::tr("Self-check");
        case Status::Online:      return QObject::tr("Online");
        case Status::Processing:  return QObject::tr("Processing");
        case Status::WarmingUp:   return QObject::tr("Warming up");
        case Status::Ready:       return QObject::tr("Ready");
        case Status::Reboot:      return QObject::tr("Reboot");
        case Status::Error:       return QObject::tr("Error");
        case Status::Disabled:    return QObject::tr("Disabled");
        case Status::Unknown:     return QObject::tr("Unknown");
    }
    return QObject::tr("Unknown");
}

const char* statusColor(presenter::EquipmentCardStatus status) {
    using Status = presenter::EquipmentCardStatus;
    switch (status) {
        case Status::Error:
            return theme::kColorAlarm;
        case Status::Offline:
        case Status::Disabled:
        case Status::Unknown:
            return theme::kColorNeutral;
        default:
            return theme::kColorOk;
    }
}

}  // namespace

QtEquipmentCard::QtEquipmentCard(std::uint32_t equipmentId,
                                 ToggleCallback onToggle, QWidget* parent)
    : QGroupBox(parent),
      equipmentId_(equipmentId),
      onToggle_(std::move(onToggle)) {
    setTitle(tr("Equipment %1").arg(equipmentId_));

    auto* layout = new QVBoxLayout(this);

    statusLabel_      = new QLabel(tr("Status: -"), this);
    consumablesLabel_ = new QLabel(tr("Supplies: -"), this);
    messageLabel_     = new QLabel("-", this);
    enableCheck_      = new QCheckBox(tr("Enabled"), this);

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
    statusLabel_->setText(tr("Status: %1").arg(statusText(viewModel.status)));
    statusLabel_->setStyleSheet(theme::coloredBold(statusColor(viewModel.status)));

    consumablesLabel_->setText(
        tr("Supplies: %1").arg(QString::fromStdString(viewModel.consumables)));
    messageLabel_->setText(QString::fromStdString(viewModel.messageStatus));

    // Reflect the enabled flag without re-triggering the toggled signal.
    const QSignalBlocker blocker(enableCheck_);
    enableCheck_->setChecked(viewModel.enabled);
}

}  // namespace app::view
