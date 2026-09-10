#pragma once

#include <QSize>
#include <QString>
#include <QWidget>

class QPaintEvent;

namespace app::view {

/// A circular value gauge (QPainter, no charting dependency): a 270-degree track
/// with a value arc coloured by tier against a target (green at/above target,
/// amber just below, red otherwise), the value in the centre and a caption
/// under it. The Qt analog of the GTK dashboard's Cairo OEE gauge.
class QtGauge : public QWidget {
public:
    explicit QtGauge(const QString& caption, double targetPct,
                     QWidget* parent = nullptr);

    void setValue(double pct);

    /// Re-set the caption (live language change).
    void setCaption(const QString& caption);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString caption_;
    double  targetPct_;
    double  value_{0.0};
};

}  // namespace app::view
