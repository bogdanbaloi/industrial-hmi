// View-model headers first (via ViewObserver.h) so their enumerators are parsed
// before any Qt header pulls in wingdi.h.
#include "src/presenter/modelview/WorkUnitViewModel.h"
#include "src/presenter/modelview/QualityCheckpointViewModel.h"

#include "src/qt/view/QtTrendsPage.h"

#include "src/qt/view/QtTheme.h"
#include "src/qt/view/QtUiDispatch.h"
#include "src/qt/view/widgets/QtLineChart.h"

#include "ui_QtTrendsPage.h"

#include <QEvent>
#include <QSizePolicy>

#include <cstddef>

namespace app::view {

QtTrendsPage::QtTrendsPage(QWidget* parent)
    : QWidget(parent), ui_(std::make_unique<Ui::QtTrendsPage>()) {
    ui_->setupUi(this);

    chart_ = new QtLineChart();
    chart_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui_->chartLayout->addWidget(chart_);

    oeeSeries_     = chart_->addSeries(tr("OEE %"), theme::kColorInfo);
    qualitySeries_ = chart_->addSeries(tr("Avg quality %"), theme::kColorOk);
}

QtTrendsPage::~QtTrendsPage() = default;

void QtTrendsPage::changeEvent(QEvent* event) {
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        ui_->retranslateUi(this);
        chart_->setSeriesName(oeeSeries_, tr("OEE %"));
        chart_->setSeriesName(qualitySeries_, tr("Avg quality %"));
    }
    QWidget::changeEvent(event);
}

void QtTrendsPage::onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) {
    // One aligned sample per tick: OEE now, plus the latest average quality.
    postToUi(this, [this, vm] {
        chart_->append(oeeSeries_, vm.oeePct);

        double sum = 0.0;
        for (const auto& entry : qualityPassRate_) {
            sum += entry.second;
        }
        const double avg =
            qualityPassRate_.empty()
                ? 0.0
                : sum / static_cast<double>(qualityPassRate_.size());
        chart_->append(qualitySeries_, avg);
    });
}

void QtTrendsPage::onQualityCheckpointChanged(
    const presenter::QualityCheckpointViewModel& vm) {
    const std::uint32_t id       = vm.checkpointId;
    const float         passRate = vm.passRate;
    postToUi(this, [this, id, passRate] { qualityPassRate_[id] = passRate; });
}

}  // namespace app::view
