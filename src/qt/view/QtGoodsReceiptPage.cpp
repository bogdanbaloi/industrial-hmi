// Presenter / view-model headers first (view-model enumerators before any Qt
// header pulls in wingdi.h).
#include "src/presenter/QualityInspectionPresenter.h"
#include "src/presenter/modelview/InspectionResultViewModel.h"

#include "src/qt/view/QtGoodsReceiptPage.h"

#include "src/qt/view/QtTheme.h"

#include "ui_QtGoodsReceiptPage.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <filesystem>

namespace app::view {

namespace {
constexpr float kPercentScale       = 100.0F;
constexpr int   kConfidenceDecimals = 1;
}  // namespace

QtGoodsReceiptPage::QtGoodsReceiptPage(
    presenter::QualityInspectionPresenter& presenter, QWidget* parent)
    : QWidget(parent),
      presenter_(presenter),
      ui_(std::make_unique<Ui::QtGoodsReceiptPage>()) {
    ui_->setupUi(this);
    connect(ui_->inspectButton, &QPushButton::clicked, this,
            [this] { chooseAndInspect(); });
}

QtGoodsReceiptPage::~QtGoodsReceiptPage() = default;

void QtGoodsReceiptPage::chooseAndInspect() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose an image"), QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty()) {
        return;
    }
    // Synchronous inspection: the callbacks below fire before this returns, on
    // the UI thread.
    presenter_.inspectFile(std::filesystem::path(path.toStdString()));
}

void QtGoodsReceiptPage::clearResults() {
    while (QLayoutItem* item = ui_->resultsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    ui_->latencyLabel->clear();
}

void QtGoodsReceiptPage::onInspectionStarted(const std::string& sourcePath) {
    ui_->pathLabel->setText(QString::fromStdString(sourcePath));
    ui_->statusLabel->setText(tr("Inspecting..."));
    ui_->statusLabel->setStyleSheet(theme::coloredBold(theme::kColorInfo));
    clearResults();
}

void QtGoodsReceiptPage::onInspectionCompleted(
    const presenter::InspectionResultViewModel& viewModel) {
    ui_->statusLabel->setText(tr("Inspection complete"));
    ui_->statusLabel->setStyleSheet(theme::coloredBold(theme::kColorOk));
    clearResults();

    bool first = true;
    for (const auto& result : viewModel.results) {
        auto* row       = new QWidget();
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        auto* name = new QLabel(QString::fromStdString(result.label));
        auto* percent = new QLabel(
            QString::number(result.confidence * kPercentScale, 'f',
                            kConfidenceDecimals) +
            "%");
        // Highlight the top-confidence row (the model's best guess).
        if (first) {
            name->setStyleSheet(theme::coloredBold(theme::kColorOk));
            percent->setStyleSheet(theme::coloredBold(theme::kColorOk));
            first = false;
        }
        rowLayout->addWidget(name);
        rowLayout->addStretch();
        rowLayout->addWidget(percent);
        ui_->resultsLayout->addWidget(row);
    }

    ui_->latencyLabel->setText(tr("Latency: %1 ms").arg(
        static_cast<qint64>(viewModel.latency.count())));
    ui_->latencyLabel->setStyleSheet(theme::coloredBold(theme::kColorNeutral));
}

void QtGoodsReceiptPage::onInspectionFailed(const std::string& sourcePath,
                                            const std::string& message) {
    ui_->pathLabel->setText(QString::fromStdString(sourcePath));
    ui_->statusLabel->setText(
        tr("Inspection failed: %1").arg(QString::fromStdString(message)));
    ui_->statusLabel->setStyleSheet(theme::coloredBold(theme::kColorAlarm));
    clearResults();
}

}  // namespace app::view
