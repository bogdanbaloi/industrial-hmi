#pragma once

// ViewObserver.h (and the view-model headers) MUST precede any Qt header.
#include "src/presenter/ViewObserver.h"

#include <QWidget>

#include <cstdint>
#include <memory>
#include <unordered_map>

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtTrendsPage;
}

namespace app::view {

class QtLineChart;

/// Live-trend page: plots session metrics (OEE and average quality) as they
/// arrive on each tick, in a rolling line chart. An app::ViewObserver driven by
/// the same DashboardPresenter the Overview uses -- no extra model access, just
/// a second observer buffering the values it already sees. Distinct from the
/// persisted historian view (a separate path); this is an in-memory session
/// trend.
///
/// Threading: the presenter callbacks can arrive on a backend thread, so each
/// buffers through the UI thread (postToUi) before touching the chart.
class QtTrendsPage : public QWidget, public app::ViewObserver {
public:
    explicit QtTrendsPage(QWidget* parent = nullptr);
    ~QtTrendsPage() override;

    QtTrendsPage(const QtTrendsPage&)            = delete;
    QtTrendsPage& operator=(const QtTrendsPage&) = delete;
    QtTrendsPage(QtTrendsPage&&)                 = delete;
    QtTrendsPage& operator=(QtTrendsPage&&)      = delete;

    void onWorkUnitChanged(const presenter::WorkUnitViewModel& vm) override;
    void onQualityCheckpointChanged(
        const presenter::QualityCheckpointViewModel& vm) override;

private:
    std::unique_ptr<Ui::QtTrendsPage>        ui_;
    QtLineChart*                             chart_{nullptr};
    int                                      oeeSeries_{-1};
    int                                      qualitySeries_{-1};
    std::unordered_map<std::uint32_t, float> qualityPassRate_;
};

}  // namespace app::view
