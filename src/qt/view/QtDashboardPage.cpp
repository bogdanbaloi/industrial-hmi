// The presenter header (which pulls the view-model enumerators) precedes any Qt
// header so they are parsed before wingdi.h defines its ERROR=0 macro.
#include "src/presenter/DashboardPresenter.h"

#include "src/qt/view/QtDashboardPage.h"

#include "src/qt/view/QtTheme.h"
#include "src/qt/view/QtUiDispatch.h"
#include "src/qt/view/widgets/QtActuatorCard.h"
#include "src/qt/view/widgets/QtEquipmentCard.h"
#include "src/qt/view/widgets/QtGauge.h"
#include "src/qt/view/widgets/QtKpiTile.h"
#include "src/qt/view/widgets/QtLineChart.h"
#include "src/qt/view/widgets/QtQualityCard.h"
#include "src/qt/view/widgets/QtUptimeDonut.h"

#include "ui_QtDashboardPage.h"

#include <QPushButton>
#include <QSizePolicy>
#include <QString>

namespace app::view {

namespace {

// Percent scale for the progress bar (progress is a 0..1 fraction).
constexpr float kPercentScale = 100.0F;

// OEE gauge target -- the "world class" 85% benchmark the GTK dashboard uses.
constexpr double kOeeTargetPct = 85.0;

}  // namespace

QtDashboardPage::QtDashboardPage(DashboardPresenter& presenter, QWidget* parent)
    : QWidget(parent),
      presenter_(presenter),
      ui_(std::make_unique<Ui::QtDashboardPage>()) {
    ui_->setupUi(this);

    // Keep the work-unit text rows at their natural height so the tall window's
    // spare vertical space collects in the trailing spacer instead of stretching
    // the gaps between these labels.
    for (auto* label : {ui_->workUnitIdLabel, ui_->productLabel,
                        ui_->statusMessageLabel}) {
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

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

    // Rich circular visuals (GTK dashboard parity): OEE gauge + uptime donut.
    oeeGauge_    = new QtGauge(tr("OEE"), kOeeTargetPct);
    uptimeDonut_ = new QtUptimeDonut();
    ui_->visualsLayout->addWidget(oeeGauge_);
    ui_->visualsLayout->addWidget(uptimeDonut_);
    ui_->visualsLayout->addStretch(1);

    // Live trend chart fills the vertical slack above the button row, so the
    // page has no dead space on the tall kiosk window. Both series are derived
    // from the same view models the tiles use -- no fabricated numbers.
    trendChart_ = new QtLineChart();
    trendChart_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    oeeSeriesIdx_     = trendChart_->addSeries(tr("OEE %"), theme::kColorInfo);
    qualitySeriesIdx_ =
        trendChart_->addSeries(tr("Avg quality %"), theme::kColorOk);
    ui_->trendLayout->addWidget(trendChart_);

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

void QtDashboardPage::setSystemState(int state) {
    // The state signal can fire on a backend thread; hop to the UI thread.
    postToUi(this, [this, state] { uptimeDonut_->setSystemState(state); });
}

// Every ViewObserver callback marshals onto the UI thread: with the
// integration backends running, these can arrive on a backend Asio thread (an
// ingest bridge writing the model), and creating / touching widgets off the UI
// thread crashes. The view model is captured by value so the deferred render
// sees a stable snapshot.

void QtDashboardPage::onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) {
    postToUi(this, [this, vm] {
        ui_->workUnitIdLabel->setText(
            tr("Order: %1").arg(QString::fromStdString(vm.workUnitId)));
        ui_->productLabel->setText(
            tr("Shipment: %1")
                .arg(QString::fromStdString(vm.productDescription)));
        ui_->statusMessageLabel->setText(
            tr("Status: %1").arg(QString::fromStdString(vm.statusMessage)));
        ui_->progressBar->setValue(
            static_cast<int>(vm.progress * kPercentScale));
        oeePct_        = vm.oeePct;
        throughputUph_ = vm.throughputUph;
        oeeGauge_->setValue(oeePct_);
        updateKpis();
        trendChart_->append(oeeSeriesIdx_, oeePct_);
        trendChart_->append(qualitySeriesIdx_, averageQuality());
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

    qualityTile_->setValue(
        QString::number(averageQuality(), 'f', kQualityDecimals) + "%");

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

double QtDashboardPage::averageQuality() const {
    if (qualityPassRate_.empty()) {
        return 0.0;
    }
    double passRateSum = 0.0;
    for (const auto& entry : qualityPassRate_) {
        passRateSum += entry.second;
    }
    return passRateSum / static_cast<double>(qualityPassRate_.size());
}

}  // namespace app::view
