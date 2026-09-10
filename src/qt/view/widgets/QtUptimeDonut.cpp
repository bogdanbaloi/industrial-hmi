#include "src/qt/view/widgets/QtUptimeDonut.h"

#include "src/qt/view/QtTheme.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <Qt>

#include <algorithm>
#include <array>
#include <numeric>

namespace app::view {

namespace {
constexpr int    kMinSize        = 150;
constexpr int    kRingThickness  = 16;
constexpr int    kRingMargin     = 14;
constexpr int    kRefreshMs      = 1000;
constexpr int    kQtAnglePerDeg  = 16;    // Qt drawArc uses 1/16-degree units
constexpr int    kFullCircleDeg  = 360;
constexpr int    kTopDeg         = 90;    // start segments at the top
constexpr int    kHalf           = 2;
constexpr int    kCaptionOffset  = 22;
constexpr int    kSecondsPerMin  = 60;
constexpr int    kTimePadWidth   = 2;
constexpr int    kTimePadBase    = 10;
constexpr double kEpsilon        = 0.001;
constexpr int    kValuePointSize = 18;
constexpr int    kCaptionPoints  = 10;

// State index -> segment colour (0 Idle, 1 Running, 2 Error, 3 Calibration).
const char* stateColor(std::size_t index) {
    switch (index) {
        case 1:  return theme::kColorOk;       // Running
        case 2:  return theme::kColorAlarm;    // Error
        case 3:  return theme::kColorInfo;     // Calibration
        default: return theme::kColorNeutral;  // Idle
    }
}
}  // namespace

QtUptimeDonut::QtUptimeDonut(QWidget* parent) : QWidget(parent) {
    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, [this] {
        accrue();
        update();
    });
    refreshTimer_->start(kRefreshMs);
}

void QtUptimeDonut::accrue() {
    const auto   now     = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - lastChange_).count();
    if (currentState_ >= 0 &&
        static_cast<std::size_t>(currentState_) < kStateCount) {
        seconds_[static_cast<std::size_t>(currentState_)] += elapsed;
    }
    lastChange_ = now;
}

void QtUptimeDonut::setSystemState(int state) {
    accrue();
    currentState_ = std::clamp(state, 0, static_cast<int>(kStateCount) - 1);
    update();
}

QSize QtUptimeDonut::sizeHint() const { return {kMinSize, kMinSize}; }

void QtUptimeDonut::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Centre the donut square in the widget so a wide host (e.g. a card) does
    // not push the ring + text off to one side and clip them.
    const int    side    = std::min(width(), height());
    const double originX = (width() - side) / 2.0;
    const double originY = (height() - side) / 2.0;
    const QRectF square(originX, originY, side, side);
    const QRectF ring(square.left() + kRingMargin, square.top() + kRingMargin,
                      side - (kHalf * kRingMargin), side - (kHalf * kRingMargin));

    const double total =
        std::accumulate(seconds_.begin(), seconds_.end(), 0.0);

    if (total < kEpsilon) {
        // Empty session: a plain neutral track.
        QPen track{QColor(theme::kColorNeutral)};
        track.setWidth(kRingThickness);
        painter.setPen(track);
        painter.drawArc(ring, 0, kFullCircleDeg * kQtAnglePerDeg);
    } else {
        int startAngle = kTopDeg * kQtAnglePerDeg;
        for (std::size_t i = 0; i < kStateCount; ++i) {
            if (seconds_[i] < kEpsilon) {
                continue;
            }
            const int span = static_cast<int>(
                (seconds_[i] / total) * kFullCircleDeg * kQtAnglePerDeg);
            QPen segment(QColor(stateColor(i)));
            segment.setWidth(kRingThickness);
            painter.setPen(segment);
            painter.drawArc(ring, startAngle, -span);
            startAngle -= span;
        }
    }

    // Centre: elapsed session time MM:SS.
    const int totalSec = static_cast<int>(total);
    const QString elapsed =
        QStringLiteral("%1:%2")
            .arg(totalSec / kSecondsPerMin)
            .arg(totalSec % kSecondsPerMin, kTimePadWidth, kTimePadBase,
                 QChar('0'));

    QFont valueFont = font();
    valueFont.setPointSize(kValuePointSize);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    painter.setPen(QColor(theme::kColorNeutral));
    painter.drawText(square.adjusted(0, 0, 0, -kCaptionOffset),
                     Qt::AlignCenter, elapsed);

    QFont captionFont = font();
    captionFont.setPointSize(kCaptionPoints);
    painter.setFont(captionFont);
    painter.setPen(QColor(theme::kColorNeutral));
    painter.drawText(square.adjusted(0, kCaptionOffset, 0, 0),
                     Qt::AlignCenter, tr("Uptime"));
}

}  // namespace app::view
