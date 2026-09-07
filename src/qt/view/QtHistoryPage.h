#pragma once

#include <QWidget>

#include <array>
#include <cstdint>
#include <memory>

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtHistoryPage;
}

namespace app::historian {
class HistoryReader;
}

namespace app::view {

class QtLineChart;

/// Persisted-historian view. A pure View (MVP): it depends only on the narrow
/// historian::HistoryReader interface -- pick a time range, query, plot. No
/// business logic, no write access. The GTK HistoryPage renders the same reader
/// the same way; this is the Qt half of the historian toolkit-independence
/// proof (REQ-ARCH-014). Distinct from QtTrendsPage, which is an in-memory live
/// session trend with no persistence.
///
/// The page is constructed only when the composition root has a non-null
/// reader (the historian opened successfully), so `reader_` is always valid.
///
/// Threading: the reader's `query()` is synchronous and runs on the UI thread
/// (a few ms for the typical range), matching the GTK page's MVP choice.
class QtHistoryPage : public QWidget {
public:
    explicit QtHistoryPage(historian::HistoryReader& reader,
                           QWidget* parent = nullptr);
    ~QtHistoryPage() override;

    QtHistoryPage(const QtHistoryPage&)            = delete;
    QtHistoryPage& operator=(const QtHistoryPage&) = delete;
    QtHistoryPage(QtHistoryPage&&)                 = delete;
    QtHistoryPage& operator=(QtHistoryPage&&)      = delete;

private:
    /// Query every series for the selected range and repaint both charts.
    void refresh();

    static constexpr std::size_t kSeriesCount = 3;

    std::unique_ptr<Ui::QtHistoryPage> ui_;
    historian::HistoryReader&          reader_;

    QtLineChart* qualityChart_{nullptr};
    QtLineChart* supplyChart_{nullptr};
    std::array<int, kSeriesCount> qualitySeries_{};
    std::array<int, kSeriesCount> supplySeries_{};
};

}  // namespace app::view
