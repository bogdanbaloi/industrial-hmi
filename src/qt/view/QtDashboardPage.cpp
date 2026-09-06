// The presenter header (which pulls the view-model enumerators) precedes any Qt
// header so they are parsed before wingdi.h defines its ERROR=0 macro.
#include "src/presenter/DashboardPresenter.h"

#include "src/qt/view/QtDashboardPage.h"

#include "src/qt/view/QtUiDispatch.h"
#include "src/qt/view/widgets/QtActuatorCard.h"
#include "src/qt/view/widgets/QtEquipmentCard.h"
#include "src/qt/view/widgets/QtKpiTile.h"
#include "src/qt/view/widgets/QtQualityCard.h"

#include "ui_QtDashboardPage.h"

#include <QPushButton>
#include <QString>

namespace app::view {

namespace {

// Percent scale for the progress bar (progress is a 0..1 fraction).
constexpr float kPercentScale = 100.0F;

}  // namespace

QtDashboardPage::QtDashboardPage(DashboardPresenter& presenter, QWidget* parent)
    : QWidget(parent),
      presenter_(presenter),
      ui_(std::make_unique<Ui::QtDashboardPage>()) {
    ui_->setupUi(this);

    // KPI tiles across the top, sharing the row width equally.
    oeeTile_        = new QtKpiTile(tr("OEE"));
    throughputTile_ = new QtKpiTile(tr("Throughput"));
    qualityTile_    = new QtKpiTile(tr("Avg quality"));
    defectsTile_    = new QtKpiTile(tr("Defects"));
    linesTile_      = new QtKpiTile(tr("Lines up"));
    for (auto* tile :
         {oeeTile_, throughputTile_, qualityTile_, defectsTile_, linesTile_}) {
        ui_->kpiLayout->addWidget(tile, 1);
    }

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

// Every ViewObserver callback marshals onto the UI thread: with the
// integration backends running, these can arrive on a backend Asio thread (an
// ingest bridge writing the model), and creating / touching widgets off the UI
// thread crashes. The view model is captured by value so the deferred render
// sees a stable snapshot.

void QtDashboardPage::onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) {
    postToUi(this, [this, vm] {
        ui_->workUnitIdLabel->setText(
            tr("Work unit: %1").arg(QString::fromStdString(vm.workUnitId)));
        ui_->productLabel->setText(
            tr("Product: %1")
                .arg(QString::fromStdString(vm.productDescription)));
        ui_->statusMessageLabel->setText(
            tr("Status: %1").arg(QString::fromStdString(vm.statusMessage)));
        ui_->progressBar->setValue(
            static_cast<int>(vm.progress * kPercentScale));
        oeePct_        = vm.oeePct;
        throughputUph_ = vm.throughputUph;
        updateKpis();
    });
}

void QtDashboardPage::onControlPanelChanged(
    const presenter::ControlPanelViewModel& vm) {
    postToUi(this, [this, vm] {
        ui_->startButton->setEnabled(vm.startEnabled);
        ui_->stopButton->setEnabled(vm.stopEnabled);
        ui_->resetButton->setEnabled(vm.resetRestartEnabled);
    });
}

void QtDashboardPage::onEquipmentCardChanged(
    const presenter::EquipmentCardViewModel& vm) {
    postToUi(this, [this, vm] {
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
        equipmentEnabled_[vm.equipmentId] = vm.enabled;
        updateKpis();
    });
}

void QtDashboardPage::onActuatorCardChanged(
    const presenter::ActuatorCardViewModel& vm) {
    postToUi(this, [this, vm] {
        auto it = actuatorCards_.find(vm.actuatorId);
        if (it == actuatorCards_.end()) {
            auto* card = new QtActuatorCard(vm.actuatorId);
            ui_->actuatorLayout->addWidget(card);
            it = actuatorCards_.emplace(vm.actuatorId, card).first;
        }
        it->second->applyViewModel(vm);
    });
}

void QtDashboardPage::onQualityCheckpointChanged(
    const presenter::QualityCheckpointViewModel& vm) {
    postToUi(this, [this, vm] {
        auto it = qualityCards_.find(vm.checkpointId);
        if (it == qualityCards_.end()) {
            auto* card = new QtQualityCard(vm.checkpointId);
            ui_->qualityLayout->addWidget(card);
            it = qualityCards_.emplace(vm.checkpointId, card).first;
        }
        it->second->applyViewModel(vm);
        qualityPassRate_[vm.checkpointId] = vm.passRate;
        qualityDefects_[vm.checkpointId]  = vm.defectsFound;
        updateKpis();
    });
}

void QtDashboardPage::updateKpis() {
    constexpr int kOeeDecimals     = 0;
    constexpr int kQualityDecimals = 1;

    oeeTile_->setValue(QString::number(oeePct_, 'f', kOeeDecimals) + "%");
    throughputTile_->setValue(
        tr("%1 uph").arg(QString::number(throughputUph_, 'f', kOeeDecimals)));

    float passRateSum = 0.0F;
    for (const auto& entry : qualityPassRate_) {
        passRateSum += entry.second;
    }
    const float avgQuality =
        qualityPassRate_.empty()
            ? 0.0F
            : passRateSum / static_cast<float>(qualityPassRate_.size());
    qualityTile_->setValue(QString::number(avgQuality, 'f', kQualityDecimals) +
                           "%");

    int defects = 0;
    for (const auto& entry : qualityDefects_) {
        defects += entry.second;
    }
    defectsTile_->setValue(QString::number(defects));

    int linesUp = 0;
    for (const auto& entry : equipmentEnabled_) {
        if (entry.second) {
            ++linesUp;
        }
    }
    linesTile_->setValue(QString("%1/%2").arg(linesUp).arg(
        static_cast<int>(equipmentEnabled_.size())));
}

}  // namespace app::view
