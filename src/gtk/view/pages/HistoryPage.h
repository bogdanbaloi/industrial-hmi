#pragma once

#include "src/gtk/view/pages/Page.h"
#include "src/gtk/view/widgets/TrendChart.h"
#include "src/historian/HistoryReader.h"

#include <gtkmm.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>

namespace app::view {

/// Time-series visualisation page.
///
/// Renders six TrendCharts (3 quality pass rates + 3 equipment supply
/// levels) plus a system-state strip across the top. Operator picks a
/// time range (Last hour / Last 24h / Last 7d) and hits Refresh; the
/// page queries the injected HistoryReader and feeds each chart from
/// the returned rows.
///
/// @design This is a pure View in the MVP sense:
///   * No business logic. The HistoryReader interface is the entire
///     contract with the rest of the system.
///   * No presenter -- the page itself is small enough that a
///     dedicated HistoryPresenter would be ceremony without value;
///     when the History track grows beyond range/refresh (filtering,
///     comparison overlays, alerts integration) the presenter split
///     becomes worth it.
///   * The page is constructed only when `Application::historyReader()`
///     is non-null, so it can rely on `reader_` being valid.
///
/// @threading All updates run on the GTK main thread; the reader's
///            `query()` is synchronous (a few ms on the typical
///            "last hour" range) so we don't punt the call onto a
///            worker thread for the MVP.
class HistoryPage : public Page {
public:
    HistoryPage(DialogManager& dialogManager,
                historian::HistoryReader& reader);
    ~HistoryPage() override;

    [[nodiscard]] Glib::ustring pageTitle() const override;
    void onLanguageChanged() override;

    /// Test seam (REQ-HISTORIAN-005). Returns the current footer text
    /// so HistoryPageTest can assert on the empty-state / sample-count
    /// branch without peeking at private members. Production callers
    /// have no use for this -- the footer is purely operator-facing.
    [[nodiscard]] Glib::ustring footerText() const {
        return footerLabel_ != nullptr ? footerLabel_->get_text()
                                       : Glib::ustring{};
    }

    /// Test seam (REQ-HISTORIAN-005). Triggers the same code path the
    /// Refresh button's signal_clicked would. Going through the public
    /// button signal in a test requires a running Gtk main loop; this
    /// shortcut keeps HistoryPageTest synchronous.
    void triggerRefreshForTest() { onRefreshClicked(); }

    /// Test seam (REQ-HISTORIAN-006). Reports whether the chart at
    /// `index` in the quality (`isQuality == true`) or supply
    /// (`isQuality == false`) group is currently shown. Reads the
    /// toggle's checked state -- the single source of truth that
    /// drives chart visibility -- so the seam survives any future
    /// show/hide transition animation.
    [[nodiscard]] bool chartVisible(bool isQuality,
                                    std::size_t index) const noexcept;

    /// Test seam (REQ-HISTORIAN-006). Unchecks the toggle at `index`
    /// in the chosen group, which fires `signal_toggled()`
    /// synchronously and runs `onToggleChart` inline -- no Gtk main
    /// loop pump needed.
    void hideChartForTest(bool isQuality, std::size_t index);

    /// Test seam (REQ-HISTORIAN-006). Re-checks the toggle at `index`,
    /// driving the chart back to visible. Symmetric counterpart to
    /// `hideChartForTest`.
    void showChartForTest(bool isQuality, std::size_t index);

private:
    /// Operator-visible time range options. Maps to a fixed lookback
    /// in milliseconds applied at query time. Stored as an enum rather
    /// than a free std::chrono to keep ComboBoxText indices stable.
    enum class Range : std::uint8_t {
        LastHour = 0,
        Last24h  = 1,
        Last7d   = 2,
    };

    void buildUi();
    void refreshAllCharts();
    void onRefreshClicked();
    void onRangeChanged();

    /// Show/hide the chart at `index` in the quality or supply group
    /// to match its toggle's checked state (REQ-HISTORIAN-006). Runs
    /// on the GTK main thread from the CheckButton's `signal_toggled`;
    /// touches only widget visibility, never the data path -- hidden
    /// charts keep getting queried so they show current data the
    /// moment the operator re-enables them.
    void onToggleChart(std::size_t index, bool isQuality);

    /// Pull data for one (field, entity) pair and push it into the
    /// chart. The chart's fixed-capacity ring buffer means very long
    /// ranges get downsampled by SQLite's LIMIT; the operator sees
    /// shape, not every sample, which is what trend charts are for.
    void populateChart(TrendChart* chart,
                       historian::FieldKind field,
                       std::uint32_t entityId);

    [[nodiscard]] std::chrono::milliseconds rangeLookback() const noexcept;

    historian::HistoryReader& reader_;

    Gtk::ComboBoxText*           rangePicker_{nullptr};
    Gtk::Button*                 refreshButton_{nullptr};
    Gtk::Label*                  footerLabel_{nullptr};
    /// Visible only while a Refresh is in flight (REQ-HISTORIAN-005).
    /// On the typical "Last hour" query the spin is sub-perceptual,
    /// but for "Last 7 days" against a cold historian the operator
    /// otherwise has no feedback that the click registered.
    Gtk::Spinner*                spinner_{nullptr};

    // 3 quality charts + 3 supply charts. Kept in a fixed array so
    // refresh iterates without branching on equipmentCount /
    // qualityCount. Matches SimulatedModel's three lines; deployments
    // that grow either count would bump this constant + add per-slot
    // construction.
    static constexpr std::size_t kQualityCount   = 3;
    static constexpr std::size_t kEquipmentCount = 3;
    std::array<TrendChart*, kQualityCount>   qualityCharts_{};
    std::array<TrendChart*, kEquipmentCount> supplyCharts_{};

    // Per-chart visibility toggles (REQ-HISTORIAN-006), one checkbox
    // per chart, parallel-indexed with the chart arrays above. All
    // checked by default; unchecking hides the matching chart.
    std::array<Gtk::CheckButton*, kQualityCount>   qualityToggles_{};
    std::array<Gtk::CheckButton*, kEquipmentCount> supplyToggles_{};

    // Auto-refresh timer. Glib::SignalTimeout fires on the GTK main
    // thread which is exactly where reader_.query() must run anyway,
    // so the callback can update widgets directly without marshalling.
    // The sigc::connection is held to disconnect on page destruction;
    // a leaked timeout would call into a dead widget tree on the next
    // tick and crash.
    sigc::connection             autoRefreshConn_;
};

}  // namespace app::view
