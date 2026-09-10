#pragma once

#include <QSize>
#include <QString>
#include <QWidget>

#include <deque>
#include <vector>

class QPaintEvent;

namespace app::view {

/// A minimal multi-series line chart (QPainter, no charting dependency) on a
/// fixed 0..100 percentage axis. Each series keeps a rolling window of the most
/// recent points, so appending scrolls the line left. Used by the Trends page
/// for live session metrics; the persisted historian view is a separate path.
class QtLineChart : public QWidget {
public:
    explicit QtLineChart(QWidget* parent = nullptr);

    /// Register a series; returns its index for `append`.
    int addSeries(const QString& name, const char* color);

    /// Rename a series' legend label (live language change); no-op if the index
    /// is out of range.
    void setSeriesName(int series, const QString& name);

    /// Append a point (clamped to 0..100) to a series and repaint. The live
    /// feed path (Overview / Trends): keeps a rolling window of recent points.
    void append(int series, double value);

    /// Replace a series' points wholesale from a batch (each clamped to
    /// 0..100) and repaint. The query path (the historian History page loads a
    /// whole result set on Refresh); unlike `append`, no rolling-window cap is
    /// applied -- the caller bounds the point count (the reader already caps it).
    void setPoints(int series, const std::vector<double>& values);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Series {
        QString           name;
        const char*       color;
        std::deque<double> points;
    };

    std::vector<Series> series_;
};

}  // namespace app::view
