#include "src/qt/view/widgets/QtQualityCard.h"

#include <QLabel>
#include <QString>
#include <QVBoxLayout>

namespace app::view {

namespace {

// One decimal place for the pass-rate percentage.
constexpr int kPassRateDecimals = 1;

const char* statusText(presenter::QualityCheckpointStatus status) {
    using Status = presenter::QualityCheckpointStatus;
    switch (status) {
        case Status::Passing:  return "Passing";
        case Status::Warning:  return "Warning";
        case Status::Critical: return "Critical";
    }
    return "Passing";
}

const char* statusColor(presenter::QualityCheckpointStatus status) {
    using Status = presenter::QualityCheckpointStatus;
    switch (status) {
        case Status::Critical: return "#c62828";  // red
        case Status::Warning:  return "#f9a825";  // amber
        case Status::Passing:  return "#2e7d32";  // green
    }
    return "#2e7d32";
}

}  // namespace

QtQualityCard::QtQualityCard(std::uint32_t checkpointId, QWidget* parent)
    : QGroupBox(parent), checkpointId_(checkpointId) {
    setTitle(QString("Checkpoint %1").arg(checkpointId_));

    auto* layout = new QVBoxLayout(this);

    statusLabel_     = new QLabel("Status: -", this);
    passRateLabel_   = new QLabel("Pass rate: -", this);
    statsLabel_      = new QLabel("-", this);
    lastDefectLabel_ = new QLabel("-", this);

    layout->addWidget(statusLabel_);
    layout->addWidget(passRateLabel_);
    layout->addWidget(statsLabel_);
    layout->addWidget(lastDefectLabel_);
}

void QtQualityCard::applyViewModel(
    const presenter::QualityCheckpointViewModel& viewModel) {
    if (!viewModel.checkpointName.empty()) {
        setTitle(QString::fromStdString(viewModel.checkpointName));
    }

    statusLabel_->setText(
        QString("Status: %1").arg(statusText(viewModel.status)));
    statusLabel_->setStyleSheet(
        QString("color: %1; font-weight: bold;").arg(statusColor(viewModel.status)));

    passRateLabel_->setText(
        QString("Pass rate: %1%")
            .arg(QString::number(viewModel.passRate, 'f', kPassRateDecimals)));

    statsLabel_->setText(QString("Inspected: %1  Defects: %2")
                             .arg(viewModel.unitsInspected)
                             .arg(viewModel.defectsFound));

    lastDefectLabel_->setText(
        viewModel.lastDefect.empty()
            ? QString("No defects")
            : QString("Last: %1").arg(QString::fromStdString(viewModel.lastDefect)));
}

}  // namespace app::view
