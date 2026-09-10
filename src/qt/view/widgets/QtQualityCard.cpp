#include "src/qt/view/widgets/QtQualityCard.h"

#include "src/qt/view/QtTheme.h"

#include <QEvent>
#include <QLabel>
#include <QObject>
#include <QString>
#include <QVBoxLayout>

namespace app::view {

namespace {

// One decimal place for the pass-rate percentage.
constexpr int kPassRateDecimals = 1;

QString statusText(presenter::QualityCheckpointStatus status) {
    using Status = presenter::QualityCheckpointStatus;
    switch (status) {
        case Status::Passing:  return QObject::tr("Passing");
        case Status::Warning:  return QObject::tr("Warning");
        case Status::Critical: return QObject::tr("Critical");
    }
    return QObject::tr("Passing");
}

const char* statusColor(presenter::QualityCheckpointStatus status) {
    using Status = presenter::QualityCheckpointStatus;
    switch (status) {
        case Status::Critical: return theme::kColorAlarm;
        case Status::Warning:  return theme::kColorWarning;
        case Status::Passing:  return theme::kColorOk;
    }
    return theme::kColorOk;
}

}  // namespace

QtQualityCard::QtQualityCard(std::uint32_t checkpointId, QWidget* parent)
    : QGroupBox(parent), checkpointId_(checkpointId) {
    setTitle(tr("Checkpoint %1").arg(checkpointId_));

    auto* layout = new QVBoxLayout(this);

    statusLabel_     = new QLabel(tr("Status: -"), this);
    passRateLabel_   = new QLabel(tr("Pass rate: -"), this);
    statsLabel_      = new QLabel("-", this);
    lastDefectLabel_ = new QLabel("-", this);

    layout->addWidget(statusLabel_);
    layout->addWidget(passRateLabel_);
    layout->addWidget(statsLabel_);
    layout->addWidget(lastDefectLabel_);
}

void QtQualityCard::applyViewModel(
    const presenter::QualityCheckpointViewModel& viewModel) {
    lastViewModel_ = viewModel;
    if (!viewModel.checkpointName.empty()) {
        setTitle(QString::fromStdString(viewModel.checkpointName));
    }

    statusLabel_->setText(tr("Status: %1").arg(statusText(viewModel.status)));
    statusLabel_->setStyleSheet(theme::coloredBold(statusColor(viewModel.status)));

    passRateLabel_->setText(
        tr("Pass rate: %1%")
            .arg(QString::number(viewModel.passRate, 'f', kPassRateDecimals)));

    statsLabel_->setText(tr("Inspected: %1  Defects: %2")
                             .arg(viewModel.unitsInspected)
                             .arg(viewModel.defectsFound));

    lastDefectLabel_->setText(
        viewModel.lastDefect.empty()
            ? tr("No defects")
            : tr("Last: %1").arg(QString::fromStdString(viewModel.lastDefect)));
}

void QtQualityCard::changeEvent(QEvent* event) {
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        if (lastViewModel_) {
            applyViewModel(*lastViewModel_);
        } else {
            setTitle(tr("Checkpoint %1").arg(checkpointId_));
            statusLabel_->setText(tr("Status: -"));
            passRateLabel_->setText(tr("Pass rate: -"));
        }
    }
    QGroupBox::changeEvent(event);
}

}  // namespace app::view
