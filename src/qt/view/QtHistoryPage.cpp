// The historian headers pull no wingdi-sensitive enumerators, but keep the
// project header before the Qt headers for include-order consistency.
#include "src/qt/view/QtHistoryPage.h"

#include "src/historian/HistoryReader.h"
#include "src/historian/HistoryRecord.h"
#include "src/qt/view/QtTheme.h"
#include "src/qt/view/widgets/QtLineChart.h"

#include "ui_QtHistoryPage.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QString>
#include <QVariant>

#include <chrono>
#include <cstdint>
#include <vector>

namespace app::view {

namespace {

// Range windows offered in the combo, as milliseconds back from now. Named so
// the query math carries no bare magic numbers.
constexpr qint64 kMsPerSecond = 1000;
constexpr qint64 kMsPerHour   = 60LL * 60LL * kMsPerSecond;
constexpr qint64 kWindowHour  = kMsPerHour;
constexpr qint64 kWindowDay   = 24LL * kMsPerHour;
constexpr qint64 kWindowWeek  = 7LL * kWindowDay;

// One colour per series index (checkpoint / equipment slot 0..2).
constexpr std::array<const char*, 3> kSeriesColors{
    theme::kColorOk, theme::kColorInfo, theme::kColorWarning};

qint64 nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Pull one series' values (timestamp-ascending) for a range into a plain
// vector the chart can plot directly.
std::vector<double> queryValues(historian::HistoryReader& reader,
                                historian::FieldKind field,
                                std::uint32_t entityId,
                                qint64 fromMs, qint64 toMs) {
    historian::QueryRange range;
    range.fromMs = fromMs;
    range.toMs   = toMs;
    const auto records = reader.query(field, entityId, range);
    std::vector<double> values;
    values.reserve(records.size());
    for (const auto& record : records) {
        values.push_back(static_cast<double>(record.value));
    }
    return values;
}

}  // namespace

QtHistoryPage::QtHistoryPage(historian::HistoryReader& reader, QWidget* parent)
    : QWidget(parent),
      ui_(std::make_unique<Ui::QtHistoryPage>()),
      reader_(reader) {
    ui_->setupUi(this);

    ui_->rangeCombo->addItem(tr("Last hour"), QVariant::fromValue(kWindowHour));
    ui_->rangeCombo->addItem(tr("Last 24 hours"),
                             QVariant::fromValue(kWindowDay));
    ui_->rangeCombo->addItem(tr("Last 7 days"),
                             QVariant::fromValue(kWindowWeek));

    // Two grouped charts (quality pass rate / equipment supply), each with one
    // series per entity, mirroring the GTK History page's grouping.
    qualityChart_ = new QtLineChart();
    supplyChart_  = new QtLineChart();
    qualityChart_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    supplyChart_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui_->qualityChartLayout->addWidget(qualityChart_);
    ui_->supplyChartLayout->addWidget(supplyChart_);

    for (std::size_t i = 0; i < kSeriesCount; ++i) {
        const char* color = kSeriesColors.at(i);
        qualitySeries_.at(i) = qualityChart_->addSeries(
            tr("Checkpoint %1").arg(i), color);
        supplySeries_.at(i) = supplyChart_->addSeries(
            tr("Equipment %1").arg(i), color);
    }

    connect(ui_->refreshButton, &QPushButton::clicked, this,
            [this] { refresh(); });

    refresh();
}

QtHistoryPage::~QtHistoryPage() = default;

void QtHistoryPage::refresh() {
    const qint64 toMs     = nowMs();
    const qint64 windowMs = ui_->rangeCombo->currentData().toLongLong();
    const qint64 fromMs   = toMs - windowMs;

    for (std::size_t i = 0; i < kSeriesCount; ++i) {
        const auto entityId = static_cast<std::uint32_t>(i);
        qualityChart_->setPoints(
            qualitySeries_.at(i),
            queryValues(reader_, historian::FieldKind::QualityPassRate,
                        entityId, fromMs, toMs));
        supplyChart_->setPoints(
            supplySeries_.at(i),
            queryValues(reader_, historian::FieldKind::EquipmentSupplyLevel,
                        entityId, fromMs, toMs));
    }

    const std::size_t total = reader_.totalSamples();
    ui_->footerLabel->setText(total == 0
                                  ? tr("No samples recorded yet")
                                  : tr("Total: %1 samples").arg(total));
}

}  // namespace app::view
