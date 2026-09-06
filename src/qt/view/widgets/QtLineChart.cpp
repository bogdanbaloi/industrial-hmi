#include "src/qt/view/widgets/QtLineChart.h"

#include "src/qt/view/QtTheme.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <Qt>

#include <algorithm>
#include <cstddef>

namespace app::view {

namespace {
constexpr int    kMinW          = 320;
constexpr int    kMinH          = 160;
constexpr int    kMaxPoints     = 120;   // rolling window (~4 min at a 2s tick)
constexpr double kAxisMax       = 100.0;
constexpr int    kGridStep      = 50;    // gridlines at 0 / 50 / 100
constexpr int    kMarginLeft    = 34;
constexpr int    kMarginRight   = 12;
constexpr int    kMarginTop     = 22;    // room for the legend
constexpr int    kMarginBottom  = 16;
constexpr int    kLegendSwatch  = 18;
constexpr int    kLegendGap     = 6;
constexpr int    kLegendSpacing = 18;
constexpr int    kLineWidth     = 2;
constexpr int    kFontPoints    = 9;
}  // namespace

QtLineChart::QtLineChart(QWidget* parent) : QWidget(parent) {}

int QtLineChart::addSeries(const QString& name, const char* color) {
    series_.push_back(Series{name, color, {}});
    return static_cast<int>(series_.size()) - 1;
}

void QtLineChart::append(int series, double value) {
    if (series < 0 || series >= static_cast<int>(series_.size())) {
        return;
    }
    auto& points = series_[static_cast<std::size_t>(series)].points;
    points.push_back(std::clamp(value, 0.0, kAxisMax));
    if (points.size() > kMaxPoints) {
        points.pop_front();
    }
    update();
}

QSize QtLineChart::sizeHint() const { return {kMinW, kMinH}; }

void QtLineChart::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QFont chartFont = font();
    chartFont.setPointSize(kFontPoints);
    painter.setFont(chartFont);

    const double left   = kMarginLeft;
    const double top    = kMarginTop;
    const double right  = width() - kMarginRight;
    const double bottom = height() - kMarginBottom;
    const double plotW  = right - left;
    const double plotH  = bottom - top;
    if (plotW <= 0 || plotH <= 0) {
        return;
    }

    // Gridlines + Y labels at 0 / 50 / 100.
    const QColor gridColor(theme::kColorNeutral);
    for (int pct = 0; pct <= static_cast<int>(kAxisMax); pct += kGridStep) {
        const double y = bottom - (pct / kAxisMax) * plotH;
        QPen gridPen(gridColor);
        gridPen.setWidth(1);
        painter.setPen(gridPen);
        painter.drawLine(QPointF(left, y), QPointF(right, y));
        painter.drawText(QRectF(0, y - kMarginTop / 2.0, left - kLegendGap,
                                kMarginTop),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(pct));
    }

    // Series polylines + legend.
    double legendX = left;
    for (const auto& series : series_) {
        const QColor color(series.color);

        // Legend entry.
        QPen legendPen(color);
        legendPen.setWidth(kLineWidth);
        painter.setPen(legendPen);
        painter.drawLine(QPointF(legendX, top - kLegendGap),
                         QPointF(legendX + kLegendSwatch, top - kLegendGap));
        painter.drawText(
            QPointF(legendX + kLegendSwatch + kLegendGap, top - kLegendGap / 2),
            series.name);
        legendX += kLegendSwatch + kLegendGap + kLegendSpacing +
                   (series.name.size() * kFontPoints);

        // Line.
        const std::size_t count = series.points.size();
        if (count < 2) {
            continue;
        }
        QPolygonF line;
        for (std::size_t i = 0; i < count; ++i) {
            const double x =
                left + (static_cast<double>(i) / static_cast<double>(count - 1)) *
                           plotW;
            const double y = bottom - (series.points[i] / kAxisMax) * plotH;
            line << QPointF(x, y);
        }
        QPen linePen(color);
        linePen.setWidth(kLineWidth);
        painter.setPen(linePen);
        painter.drawPolyline(line);
    }
}

}  // namespace app::view
