#pragma once

#include <QSize>
#include <QWidget>

#include <array>
#include <chrono>
#include <cstddef>

class QPaintEvent;
class QTimer;

namespace app::view {

/// Session-uptime donut (QPainter): accumulates wall time spent in each system
/// state (Idle / Running / Error / Calibration) and draws it as a ring of
/// coloured segments, with the elapsed session time in the centre. The Qt
/// analog of the GTK dashboard's Cairo uptime donut. `setSystemState` is fed
/// from the presenter's state signal; an internal timer animates the current
/// segment growing.
class QtUptimeDonut : public QWidget {
public:
    explicit QtUptimeDonut(QWidget* parent = nullptr);

    void setSystemState(int state);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr std::size_t kStateCount = 4;

    void accrue();

    std::array<double, kStateCount>       seconds_{};
    int                                   currentState_{0};
    std::chrono::steady_clock::time_point lastChange_{
        std::chrono::steady_clock::now()};
    QTimer*                               refreshTimer_{nullptr};
};

}  // namespace app::view
