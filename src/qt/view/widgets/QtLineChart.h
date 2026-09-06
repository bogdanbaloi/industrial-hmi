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

    /// Append a point (clamped to 0..100) to a series and repaint.
    void append(int series, double value);

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
