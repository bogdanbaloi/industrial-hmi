#include "src/qt/view/widgets/QtGauge.h"

#include "src/qt/view/QtTheme.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <Qt>

#include <algorithm>

namespace app::view {

namespace {
constexpr int    kMinSize          = 150;
constexpr int    kQtAnglePerDeg    = 16;    // Qt drawArc uses 1/16-degree units
constexpr int    kStartAngleDeg    = 225;   // gap at the bottom
constexpr int    kSpanDeg          = 270;
constexpr double kFullPct          = 100.0;
constexpr double kWarnBandPct      = 5.0;    // amber within 5% below target
constexpr int    kArcThickness     = 14;
constexpr int    kArcMargin        = 16;
constexpr int    kValuePointSize   = 22;
constexpr int    kCaptionPointSize = 10;
constexpr int    kHalf             = 2;
constexpr int    kQuarter          = 4;

const char* tierColor(double value, double target) {
    if (value >= target) {
        return theme::kColorOk;
    }
    if (value >= target - kWarnBandPct) {
        return theme::kColorWarning;
    }
    return theme::kColorAlarm;
}
}  // namespace

QtGauge::QtGauge(const QString& caption, double targetPct, QWidget* parent)
    : QWidget(parent), caption_(caption), targetPct_(targetPct) {}

void QtGauge::setCaption(const QString& caption) {
    caption_ = caption;
    update();
}

void QtGauge::setValue(double pct) {
    value_ = pct;
    update();
}

QSize QtGauge::sizeHint() const { return {kMinSize, kMinSize}; }

void QtGauge::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Centre the gauge square in the widget so a wide host (e.g. a card) does
    // not push the arc + text off to one side and clip them.
    const int    side    = std::min(width(), height());
    const double originX = (width() - side) / 2.0;
    const double originY = (height() - side) / 2.0;
    const QRectF square(originX, originY, side, side);
    const QRectF arcRect(square.left() + kArcMargin, square.top() + kArcMargin,
                         side - (kHalf * kArcMargin), side - (kHalf * kArcMargin));

    // Background track.
    QPen track{QColor(theme::kColorNeutral)};
    track.setWidth(kArcThickness);
    track.setCapStyle(Qt::RoundCap);
    painter.setPen(track);
    painter.drawArc(arcRect, kStartAngleDeg * kQtAnglePerDeg,
                    -kSpanDeg * kQtAnglePerDeg);

    // Value arc, coloured by tier.
    const double  fraction = std::clamp(value_ / kFullPct, 0.0, 1.0);
    const QColor  tier(tierColor(value_, targetPct_));
    QPen          arc(tier);
    arc.setWidth(kArcThickness);
    arc.setCapStyle(Qt::RoundCap);
    painter.setPen(arc);
    painter.drawArc(arcRect, kStartAngleDeg * kQtAnglePerDeg,
                    -static_cast<int>(kSpanDeg * fraction) * kQtAnglePerDeg);

    // Centre value (tier-coloured so it reads on any palette ground).
    QFont valueFont = font();
    valueFont.setPointSize(kValuePointSize);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    painter.setPen(tier);
    painter.drawText(square.adjusted(0, 0, 0, -static_cast<double>(side) / kQuarter),
                     Qt::AlignCenter, QString::number(value_, 'f', 0) + "%");

    // Caption below.
    QFont captionFont = font();
    captionFont.setPointSize(kCaptionPointSize);
    painter.setFont(captionFont);
    painter.setPen(QColor(theme::kColorNeutral));
    painter.drawText(square.adjusted(0, static_cast<double>(side) / kQuarter, 0, 0),
                     Qt::AlignCenter, caption_);
}

}  // namespace app::view
