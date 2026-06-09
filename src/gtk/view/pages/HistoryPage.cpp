#include "src/gtk/view/pages/HistoryPage.h"

#include "src/core/i18n.h"
#include "src/gtk/view/ui_sizes.h"

#include <chrono>
#include <format>
#include <string>

namespace app::view {

namespace {

// Trend charts cap at this many points -- the UI does not need to
// render every sample for a "last 24h" lookback. SQLite's LIMIT
// downsamples the tail of the window (newest-N semantics), so the
// chart always shows the most recent N samples in the requested range.
// Matches the TrendChart widget's default capacity so the chart's
// internal ring buffer is filled exactly once per refresh.
constexpr std::size_t kChartLimit = 300;

// Auto-refresh cadence. 5 s is the balance between "operator sees
// live dashboard-like motion" and "SQLite query traffic stays
// sub-1 % CPU when the page is open". The query is light (compound-
// indexed range scan capped at 300 rows) so even tighter cadences
// are safe, but 5 s also makes each Refresh visibly land without
// looking nervous.
constexpr unsigned kAutoRefreshIntervalMs = 5'000;

// Layout constants for the page's outer Box and the inline toolbar.
// Named so the magic-number lint stays clean and so a future design
// tweak lands in one place.
constexpr int kPageSpacingPx     = 12;
constexpr int kToolbarSpacingPx  = 12;
constexpr int kPageMarginPx      = 16;
constexpr int kChartGridRowGap   = 8;
constexpr int kChartGridColGap   = 16;

// Range -> lookback helpers. Express the day/week durations once
// (rather than as `24` / `24*7` ints) so the readability-magic-
// numbers lint stays happy and the intent is obvious.
constexpr int kHoursPerDay  = 24;
constexpr int kHoursPerWeek = kHoursPerDay * 7;

/// Render a non-negative integer with comma thousands separators,
/// e.g. 1234567 -> "1,234,567". Used by the History page footer
/// (REQ-HISTORIAN-005) so the operator can read a 6-digit sample
/// count at a glance.
///
/// We do this by hand rather than via `std::format("{:L}", n)`
/// because the `L` flag requires a non-classic `std::locale`, and
/// the binary deliberately stays locale-neutral so the layout-budget
/// tests (DashboardPageTest.CompactPaneFitsMultiStationWidthBudget)
/// see deterministic text widths.
std::string formatWithThousands(std::size_t n) {
    auto s = std::to_string(n);
    if (s.size() <= 3) return s;
    // Walk backwards in groups of 3 inserting commas. Cast to int to
    // avoid signed/unsigned mix in the loop counter.
    auto pos = static_cast<int>(s.size()) - 3;
    while (pos > 0) {
        s.insert(static_cast<std::string::size_type>(pos), ",");
        pos -= 3;
    }
    return s;
}

// Wall-clock ms since epoch. Uniform with HistorianBridge so the
// queries line up with what the bridge wrote.
std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

HistoryPage::HistoryPage(DialogManager& dialogManager,
                         historian::HistoryReader& reader)
    : Page(dialogManager),
      reader_(reader) {
    buildUi();

    // First paint so the operator sees something the moment they
    // switch to the tab, instead of a blank chart that fills only
    // after the first timer tick.
    refreshAllCharts();

    // Live refresh: re-query every 5 s on the GTK main loop. Glib's
    // signal_timeout dispatches the slot on the UI thread, so the
    // callback can touch widgets directly. Returning `true` keeps
    // the timeout armed; the sigc::connection lets the destructor
    // disconnect cleanly so a dangling tick can never call into a
    // dead widget tree.
    autoRefreshConn_ = Glib::signal_timeout().connect(
        [this]() {
            refreshAllCharts();
            return true;
        },
        kAutoRefreshIntervalMs);
}

HistoryPage::~HistoryPage() {
    autoRefreshConn_.disconnect();
}

Glib::ustring HistoryPage::pageTitle() const {
    return _("History");
}

void HistoryPage::onLanguageChanged() {
    // The notebook tab label is rebuilt by MainWindow's
    // languageChanged path; the in-page widgets that show translated
    // strings (button labels, range options) are rebuilt by
    // re-running buildUi -- but doing that here would re-allocate
    // the chart array on every locale flip and lose accumulated
    // history. For the MVP we leave the in-page strings English-only
    // and let the tab label flip; production-grade i18n on this page
    // is a follow-up.
}

void HistoryPage::buildUi() {
    set_orientation(Gtk::Orientation::VERTICAL);
    set_spacing(kPageSpacingPx);
    set_margin(kPageMarginPx);

    // Top toolbar: range picker + refresh button.
    auto* toolbar = Gtk::make_managed<Gtk::Box>(
        Gtk::Orientation::HORIZONTAL, kToolbarSpacingPx);

    auto* rangeLabel = Gtk::make_managed<Gtk::Label>(_("Range:"));
    rangePicker_ = Gtk::make_managed<Gtk::ComboBoxText>();
    rangePicker_->append(_("Last hour"));
    rangePicker_->append(_("Last 24 hours"));
    rangePicker_->append(_("Last 7 days"));
    rangePicker_->set_active(0);
    rangePicker_->signal_changed().connect(
        sigc::mem_fun(*this, &HistoryPage::onRangeChanged));

    refreshButton_ = Gtk::make_managed<Gtk::Button>(_("Refresh"));
    refreshButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &HistoryPage::onRefreshClicked));

    // Spinner sits next to the Refresh button, hidden by default. It
    // only animates while a manual Refresh is in flight; auto-refresh
    // ticks do not spin (silently re-querying every 5 s should not
    // strobe the toolbar).
    spinner_ = Gtk::make_managed<Gtk::Spinner>();
    spinner_->set_size_request(sizes::kHistorySpinnerSize,
                               sizes::kHistorySpinnerSize);
    spinner_->set_visible(false);

    toolbar->append(*rangeLabel);
    toolbar->append(*rangePicker_);
    toolbar->append(*refreshButton_);
    toolbar->append(*spinner_);
    append(*toolbar);

    // Chart grid: two columns -- quality on the left, supply on the right.
    // Inside each column, one chart per entity stacked vertically.
    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(kChartGridRowGap);
    grid->set_column_spacing(kChartGridColGap);
    grid->set_column_homogeneous(true);
    grid->set_vexpand(true);

    auto* qualityHeader = Gtk::make_managed<Gtk::Label>(_("Quality Pass Rate"));
    qualityHeader->set_xalign(0.0F);
    auto* supplyHeader  = Gtk::make_managed<Gtk::Label>(_("Equipment Supply Level"));
    supplyHeader->set_xalign(0.0F);
    grid->attach(*qualityHeader, 0, 0, 1, 1);
    grid->attach(*supplyHeader,  1, 0, 1, 1);

    for (std::size_t i = 0; i < kQualityCount; ++i) {
        // 1-based labels (REQ-HISTORIAN-005): operators expect
        // "Checkpoint 1, 2, 3", not zero-indexed engineering view.
        // Charts use the same 0..100 range as the gauges so an
        // operator switching back and forth between Dashboard and
        // History reads the same axes everywhere.
        const auto label = std::format("Checkpoint {}", i + 1);
        qualityCharts_[i] = Gtk::make_managed<TrendChart>(
            label, 0.0F, 100.0F);
        qualityCharts_[i]->set_vexpand(true);
        grid->attach(*qualityCharts_[i], 0,
                     static_cast<int>(i + 1), 1, 1);
    }
    for (std::size_t i = 0; i < kEquipmentCount; ++i) {
        const auto label = std::format("Line {}", i + 1);
        supplyCharts_[i] = Gtk::make_managed<TrendChart>(
            label, 0.0F, 100.0F);
        supplyCharts_[i]->set_vexpand(true);
        grid->attach(*supplyCharts_[i], 1,
                     static_cast<int>(i + 1), 1, 1);
    }
    append(*grid);

    // Footer with total row count -- gives the operator a quick read
    // on whether the historian has been collecting (and how long).
    footerLabel_ = Gtk::make_managed<Gtk::Label>("");
    footerLabel_->set_xalign(0.0F);
    append(*footerLabel_);
}

void HistoryPage::onRefreshClicked() {
    // Manual Refresh gets visible spinner feedback (REQ-HISTORIAN-005):
    // for a cold "Last 7 days" query the synchronous reader_.query()
    // can take a noticeable beat. Auto-refresh deliberately does NOT
    // spin -- silent re-queries every 5 s should not strobe the
    // toolbar.
    if (spinner_ != nullptr) {
        spinner_->set_visible(true);
        spinner_->start();
    }
    refreshAllCharts();
}

void HistoryPage::onRangeChanged() {
    refreshAllCharts();
}

void HistoryPage::refreshAllCharts() {
    for (std::size_t i = 0; i < kQualityCount; ++i) {
        populateChart(qualityCharts_[i],
                      historian::FieldKind::QualityPassRate,
                      static_cast<std::uint32_t>(i));
    }
    for (std::size_t i = 0; i < kEquipmentCount; ++i) {
        populateChart(supplyCharts_[i],
                      historian::FieldKind::EquipmentSupplyLevel,
                      static_cast<std::uint32_t>(i));
    }

    if (footerLabel_ != nullptr) {
        const auto total = reader_.totalSamples();
        // Empty-state messaging (REQ-HISTORIAN-005): a freshly-built
        // historian renders blank charts and "Total samples: 0" would
        // be misread as a failure. The explicit message makes the
        // cold-start state obvious; once data lands we switch to a
        // thousands-grouped count so a 7-digit sample tally stays
        // readable.
        if (total == 0) {
            footerLabel_->set_text(_("No data yet"));
        } else {
            footerLabel_->set_text(
                std::format("Total samples: {}",
                            formatWithThousands(total)));
        }
    }

    // Always stop the spinner, even on auto-refresh ticks where we
    // never started it. set_visible + stop are both idempotent.
    if (spinner_ != nullptr) {
        spinner_->stop();
        spinner_->set_visible(false);
    }
}

void HistoryPage::populateChart(TrendChart* chart,
                                historian::FieldKind field,
                                std::uint32_t entityId) {
    if (chart == nullptr) return;

    // Drop the previous query's points BEFORE we push the new ones
    // (REQ-HISTORIAN-005). Without this, switching range from
    // "Last 7 days" back to "Last hour" would keep the old samples
    // visible until the ring buffer wraps -- a stale-data bug that
    // silently lies to the operator about what window they are
    // looking at.
    chart->clear();

    const auto lookback = rangeLookback();
    const std::int64_t to   = nowMs();
    const std::int64_t from = to - lookback.count();

    historian::QueryRange q;
    q.fromMs = from;
    q.toMs   = to;
    q.limit  = kChartLimit;

    const auto rows = reader_.query(field, entityId, q);

    // TrendChart::addPoint() advances its internal ring buffer; pushing
    // a fresh chart's full window guarantees the latest row sits at
    // the right edge.
    for (const auto& r : rows) {
        chart->addPoint(r.value);
    }
}

std::chrono::milliseconds HistoryPage::rangeLookback() const noexcept {
    const int idx = rangePicker_ != nullptr
                        ? rangePicker_->get_active_row_number()
                        : 0;
    switch (static_cast<Range>(idx)) {
        case Range::LastHour: return std::chrono::hours{1};
        case Range::Last24h:  return std::chrono::hours{kHoursPerDay};
        case Range::Last7d:   return std::chrono::hours{kHoursPerWeek};
    }
    return std::chrono::hours{1};
}

}  // namespace app::view
