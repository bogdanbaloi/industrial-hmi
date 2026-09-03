// Model + presenter headers first so ProductionError::ERROR and
// StatusZoneViewModel::Severity::ERROR are parsed before any Qt header pulls
// in wingdi.h (ERROR=0 macro).
#include "src/model/SimulatedModel.h"
#include "src/presenter/DashboardPresenter.h"

#include "src/qt/view/QtDashboardWindow.h"

#include "src/qt/view/widgets/QtActuatorCard.h"
#include "src/qt/view/widgets/QtEquipmentCard.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

// Qt has now pulled in wingdi.h on Windows. Drop its ERROR macro so the
// StatusZoneViewModel::Severity::ERROR enumerator can be named below. We do
// not use the Win32 ERROR constant anywhere in this file.
#ifdef ERROR
#undef ERROR
#endif

namespace app::view {

namespace {

// Initial window size. Named so the magic-number lint stays quiet.
constexpr int kWindowWidth  = 480;
constexpr int kWindowHeight = 360;

// Percent scale for the progress bar (progress is a 0..1 fraction).
constexpr float kPercentScale = 100.0F;

QString bannerStyle(const char* background) {
    return QString("padding: 8px; font-weight: bold; color: white; "
                   "background: %1;")
        .arg(background);
}

const char* severityColor(presenter::StatusZoneViewModel::Severity severity) {
    using Severity = presenter::StatusZoneViewModel::Severity;
    switch (severity) {
        case Severity::ERROR:
            return "#c62828";  // red
        case Severity::WARNING:
            return "#f9a825";  // amber
        case Severity::INFO:
            return "#1565c0";  // blue
        case Severity::NONE:
            return "#2e7d32";  // green
    }
    return "#2e7d32";
}

}  // namespace

QtDashboardWindow::QtDashboardWindow(DashboardPresenter& presenter,
                                     QWidget* parent)
    : QMainWindow(parent), presenter_(presenter) {
    buildUi();
}

QtDashboardWindow::~QtDashboardWindow() = default;

void QtDashboardWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);

    statusBanner_ = new QLabel("System Idle", central);
    statusBanner_->setStyleSheet(bannerStyle("#2e7d32"));
    layout->addWidget(statusBanner_);

    workUnitIdLabel_    = new QLabel("Work unit: -", central);
    productLabel_       = new QLabel("Product: -", central);
    statusMessageLabel_ = new QLabel("Status: -", central);
    layout->addWidget(workUnitIdLabel_);
    layout->addWidget(productLabel_);
    layout->addWidget(statusMessageLabel_);

    progressBar_ = new QProgressBar(central);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    layout->addWidget(progressBar_);

    // Equipment and actuator sections. Cards are added lazily by the render
    // helpers as the presenter first reports each id.
    auto* equipmentBox = new QGroupBox("Equipment", central);
    equipmentLayout_ = new QHBoxLayout(equipmentBox);
    layout->addWidget(equipmentBox);

    auto* actuatorBox = new QGroupBox("Actuators", central);
    actuatorLayout_ = new QHBoxLayout(actuatorBox);
    layout->addWidget(actuatorBox);

    auto* buttonRow = new QHBoxLayout();
    startButton_ = new QPushButton("Start", central);
    stopButton_  = new QPushButton("Stop", central);
    resetButton_ = new QPushButton("Reset", central);
    buttonRow->addWidget(startButton_);
    buttonRow->addWidget(stopButton_);
    buttonRow->addWidget(resetButton_);
    layout->addLayout(buttonRow);

    // Back-channel: buttons call the SAME presenter methods the GTK page and
    // the console view call. These run on the UI thread.
    connect(startButton_, &QPushButton::clicked, this,
            [this] { presenter_.onStartClicked(); });
    connect(stopButton_, &QPushButton::clicked, this,
            [this] { presenter_.onStopClicked(); });
    connect(resetButton_, &QPushButton::clicked, this,
            [this] { presenter_.onResetRestartClicked(); });

    setCentralWidget(central);
    setWindowTitle("Industrial HMI (Qt)");
    resize(kWindowWidth, kWindowHeight);
}

// ViewObserver overrides -- marshal onto the UI thread, then render.

void QtDashboardWindow::onWorkUnitChanged(
    const presenter::WorkUnitViewModel& vm) {
    QMetaObject::invokeMethod(
        this, [this, vm] { renderWorkUnit(vm); }, Qt::QueuedConnection);
}

void QtDashboardWindow::onControlPanelChanged(
    const presenter::ControlPanelViewModel& vm) {
    QMetaObject::invokeMethod(
        this, [this, vm] { renderControlPanel(vm); }, Qt::QueuedConnection);
}

void QtDashboardWindow::onStatusZoneChanged(
    const presenter::StatusZoneViewModel& vm) {
    QMetaObject::invokeMethod(
        this, [this, vm] { renderStatusZone(vm); }, Qt::QueuedConnection);
}

// Render helpers -- always on the UI thread.

void QtDashboardWindow::renderWorkUnit(const presenter::WorkUnitViewModel& vm) {
    workUnitIdLabel_->setText(
        QString("Work unit: %1").arg(QString::fromStdString(vm.workUnitId)));
    productLabel_->setText(QString("Product: %1")
                               .arg(QString::fromStdString(vm.productDescription)));
    statusMessageLabel_->setText(
        QString("Status: %1").arg(QString::fromStdString(vm.statusMessage)));
    progressBar_->setValue(static_cast<int>(vm.progress * kPercentScale));
}

void QtDashboardWindow::renderControlPanel(
    const presenter::ControlPanelViewModel& vm) {
    startButton_->setEnabled(vm.startEnabled);
    stopButton_->setEnabled(vm.stopEnabled);
    resetButton_->setEnabled(vm.resetRestartEnabled);
}

void QtDashboardWindow::renderStatusZone(
    const presenter::StatusZoneViewModel& vm) {
    if (!vm.message.empty()) {
        statusBanner_->setText(QString::fromStdString(vm.message));
    }
    statusBanner_->setStyleSheet(bannerStyle(severityColor(vm.severity)));
}

void QtDashboardWindow::onEquipmentCardChanged(
    const presenter::EquipmentCardViewModel& vm) {
    QMetaObject::invokeMethod(
        this, [this, vm] { renderEquipmentCard(vm); }, Qt::QueuedConnection);
}

void QtDashboardWindow::onActuatorCardChanged(
    const presenter::ActuatorCardViewModel& vm) {
    QMetaObject::invokeMethod(
        this, [this, vm] { renderActuatorCard(vm); }, Qt::QueuedConnection);
}

void QtDashboardWindow::renderEquipmentCard(
    const presenter::EquipmentCardViewModel& vm) {
    auto it = equipmentCards_.find(vm.equipmentId);
    if (it == equipmentCards_.end()) {
        auto* card = new QtEquipmentCard(
            vm.equipmentId, [this](std::uint32_t id, bool enabled) {
                presenter_.onEquipmentToggled(id, enabled);
            });
        equipmentLayout_->addWidget(card);
        it = equipmentCards_.emplace(vm.equipmentId, card).first;
    }
    it->second->applyViewModel(vm);
}

void QtDashboardWindow::renderActuatorCard(
    const presenter::ActuatorCardViewModel& vm) {
    auto it = actuatorCards_.find(vm.actuatorId);
    if (it == actuatorCards_.end()) {
        auto* card = new QtActuatorCard(vm.actuatorId);
        actuatorLayout_->addWidget(card);
        it = actuatorCards_.emplace(vm.actuatorId, card).first;
    }
    it->second->applyViewModel(vm);
}

}  // namespace app::view
