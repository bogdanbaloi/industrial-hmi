#pragma once

#include "src/gtk/view/colors.h"

#include <gtkmm.h>

#include <cstddef>
#include <string>
#include <vector>

namespace app::view {

/// Multi-segment donut chart widget for the dashboard.
///
/// Used by Phase 8C to render the session-uptime breakdown
/// (% time the system has spent in each SystemState -- Running /
/// Idle / Calibration / Error), but the widget is fully generic:
/// callers hand in a list of (label, value, colour) segments and
/// the widget normalises shares + renders the donut.
///
/// Layout:
///   * Concentric ring -- outer radius fits the smaller of width
///     and height minus a small margin, ring thickness configurable
///     via kRingThicknessRatio.
///   * Segments drawn clockwise starting at 12 o'clock; each
///     segment painted as a Cairo arc in its assigned colour.
///   * Optional center labels (title big bold, subtitle small)
///     for the headline number ("87%") + supporting tag
///     ("uptime today").
///
/// @design Inherits Gtk::DrawingArea and paints with Cairo, same
///         shape as QualityGauge / TrendChart / BigNumberCard.
///         Theme awareness walks the toplevel for the .light-mode
///         class -- centre-text colours pick light/dark variants.
///
/// @threading GTK main thread only. setSegments() copies the
///            vector and calls queue_draw(); callers from other
///            threads must marshal through Glib::signal_idle().
class DonutChartWidget : public Gtk::DrawingArea {
public:
    /// One slice of the donut. Values are interpreted as shares
    /// (the widget normalises by the sum), so callers can pass
    /// either percentages summing to ~100, raw counts, or seconds
    /// spent in each state -- whatever is convenient. A zero or
    /// negative value collapses that segment (skipped at paint).
    struct Segment {
        Glib::ustring label;   ///< Tooltip / future legend text.
        double        value;   ///< Share (any positive scale).
        Rgb           color;   ///< Cairo-space RGB (0.0 - 1.0).
    };

    DonutChartWidget();
    ~DonutChartWidget() override = default;

    DonutChartWidget(const DonutChartWidget&)            = delete;
    DonutChartWidget& operator=(const DonutChartWidget&) = delete;
    DonutChartWidget(DonutChartWidget&&)                 = delete;
    DonutChartWidget& operator=(DonutChartWidget&&)      = delete;

    /// Replace the segment list and queue a redraw. Empty list is
    /// a valid input and leaves an empty ring (the centre labels
    /// still render, useful for "no data yet" states).
    void setSegments(std::vector<Segment> segments);

    /// Big bold text drawn in the centre of the ring. Typically a
    /// headline number ("87%" or "12h 4m"). Empty string draws
    /// nothing.
    void setCenterTitle(const Glib::ustring& text);

    /// Smaller muted text drawn under the centre title. Typically
    /// a unit or context tag ("uptime today"). Empty string draws
    /// nothing.
    void setCenterSubtitle(const Glib::ustring& text);

    /// Accessors -- mostly for tests that want to assert on what
    /// the widget will paint next.
    [[nodiscard]] const std::vector<Segment>& segments() const noexcept { return segments_; }
    [[nodiscard]] const Glib::ustring& centerTitle()    const noexcept { return centerTitle_; }
    [[nodiscard]] const Glib::ustring& centerSubtitle() const noexcept { return centerSubtitle_; }

    /// Default minimum content size. Public so DashboardPage can
    /// budget the donut's slot without inspecting CSS.
    static constexpr int kMinContentWidth  = 200;
    static constexpr int kMinContentHeight = 200;

private:
    void onDraw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    std::vector<Segment> segments_;
    Glib::ustring        centerTitle_;
    Glib::ustring        centerSubtitle_;
};

}  // namespace app::view
