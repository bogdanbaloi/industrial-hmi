// Model + presenter headers first so ProductionError::ERROR and
// StatusZoneViewModel::Severity::ERROR are parsed before any Qt header pulls in
// wingdi.h (ERROR=0 macro).
#include "src/model/SimulatedModel.h"
#include "src/presenter/DashboardPresenter.h"

#include "src/qt/view/QtDashboardPage.h"

#include "src/qt/view/QtTheme.h"
#include "src/qt/view/widgets/QtActuatorCard.h"
#include "src/qt/view/widgets/QtEquipmentCard.h"
#include "src/qt/view/widgets/QtQualityCard.h"

#include "ui_QtDashboardPage.h"

#include <QPushButton>
#include <QString>

// Qt has now pulled in wingdi.h on Windows. Drop its ERROR macro so the
// StatusZoneViewModel::Severity::ERROR enumerator can be named below.
#ifdef ERROR
#undef ERROR
#endif

namespace app::view {

namespace {

// Percent scale for the progress bar (progress is a 0..1 fraction).
constexpr float kPercentScale = 100.0F;

const char* severityColor(presenter::StatusZoneViewModel::Severity severity) {
    using Severity = presenter::StatusZoneViewModel::Severity;
    switch (severity) {
        case Severity::ERROR:
            return theme::kColorAlarm;
        case Severity::WARNING:
            return theme::kColorWarning;
        case Severity::INFO:
            return theme::kColorInfo;
        case Severity::NONE:
            return theme::kColorOk;
    }
    return theme::kColorOk;
}

}  // namespace

QtDashboardPage::QtDashboardPage(DashboardPresenter& presenter, QWidget* parent)
    : QWidget(parent),
      presenter_(presenter),
      ui_(std::make_unique<Ui::QtDashboardPage>()) {
    ui_->setupUi(this);
    ui_->statusBanner->setStyleSheet(theme::bannerStyle(theme::kColorOk));

    // Back-channel: buttons call the SAME presenter methods the GTK page and the
    // console view call.
    connect(ui_->startButton, &QPushButton::clicked, this,
            [this] { presenter_.onStartClicked(); });
    connect(ui_->stopButton, &QPushButton::clicked, this,
            [this] { presenter_.onStopClicked(); });
    connect(ui_->resetButton, &QPushButton::clicked, this,
            [this] { presenter_.onResetRestartClicked(); });
}

QtDashboardPage::~QtDashboardPage() = default;

void QtDashboardPage::onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) {
    ui_->workUnitIdLabel->setText(
        tr("Work unit: %1").arg(QString::fromStdString(vm.workUnitId)));
    ui_->productLabel->setText(
        tr("Product: %1").arg(QString::fromStdString(vm.productDescription)));
    ui_->statusMessageLabel->setText(
        tr("Status: %1").arg(QString::fromStdString(vm.statusMessage)));
    ui_->progressBar->setValue(static_cast<int>(vm.progress * kPercentScale));
}

void QtDashboardPage::onControlPanelChanged(
    const presenter::ControlPanelViewModel& vm) {
    ui_->startButton->setEnabled(vm.startEnabled);
    ui_->stopButton->setEnabled(vm.stopEnabled);
    ui_->resetButton->setEnabled(vm.resetRestartEnabled);
}

void QtDashboardPage::onStatusZoneChanged(
    const presenter::StatusZoneViewModel& vm) {
    if (!vm.message.empty()) {
        ui_->statusBanner->setText(QString::fromStdString(vm.message));
    }
    ui_->statusBanner->setStyleSheet(theme::bannerStyle(severityColor(vm.severity)));
}

void QtDashboardPage::onEquipmentCardChanged(
    const presenter::EquipmentCardViewModel& vm) {
    auto it = equipmentCards_.find(vm.equipmentId);
    if (it == equipmentCards_.end()) {
        auto* card = new QtEquipmentCard(
            vm.equipmentId, [this](std::uint32_t id, bool enabled) {
                presenter_.onEquipmentToggled(id, enabled);
            });
        ui_->equipmentLayout->addWidget(card);
        it = equipmentCards_.emplace(vm.equipmentId, card).first;
    }
    it->second->applyViewModel(vm);
}

void QtDashboardPage::onActuatorCardChanged(
    const presenter::ActuatorCardViewModel& vm) {
    auto it = actuatorCards_.find(vm.actuatorId);
    if (it == actuatorCards_.end()) {
        auto* card = new QtActuatorCard(vm.actuatorId);
        ui_->actuatorLayout->addWidget(card);
        it = actuatorCards_.emplace(vm.actuatorId, card).first;
    }
    it->second->applyViewModel(vm);
}

void QtDashboardPage::onQualityCheckpointChanged(
    const presenter::QualityCheckpointViewModel& vm) {
    auto it = qualityCards_.find(vm.checkpointId);
    if (it == qualityCards_.end()) {
        auto* card = new QtQualityCard(vm.checkpointId);
        ui_->qualityLayout->addWidget(card);
        it = qualityCards_.emplace(vm.checkpointId, card).first;
    }
    it->second->applyViewModel(vm);
}

}  // namespace app::view
