#pragma once

#include <gtkmm.h>

#include <cstdint>

namespace app::view {

/// Card-style KPI widget for the dashboard top strip.
///
/// Renders a single big metric (OEE / Throughput / Pass Rate /
/// whatever the dashboard wants to lead with) in three vertically
/// stacked rows:
///
///   * small label (top) -- the metric name, e.g. "OEE"
///   * huge bold number + unit (middle), e.g. "87.3%"
///   * delta vs target (bottom) with up/down arrow, e.g. "^ 2.3 vs
///     85.0 target". Only drawn when a target has been set via
///     setTarget(); otherwise the delta row is omitted.
///
/// A 4 px coloured strip on the left edge encodes the status tier
/// (Ok / Warning / Critical). The status also drives the delta arrow
/// colour and the value colour when the metric is in trouble, so an
/// operator scanning the top strip from across the cell catches a
/// red OEE card without reading the number.
///
/// @design Inherits Gtk::DrawingArea and paints with Cairo (same
///         shape as QualityGauge and TrendChart). Theme awareness
///         walks the widget tree to look for the `.light-mode` CSS
///         class on the toplevel window, identical to QualityGauge's
///         pattern -- shared helper in colors.h.
///
/// @threading GTK main thread only. Setters call queue_draw() but do
///            not lock; multi-threaded sources should marshal through
///            Glib::signal_idle().
///
/// @i18n The label and unit strings are pass-through -- callers
///       provide the gettext result. The widget never concatenates
///       translatable fragments.
class BigNumberCard : public Gtk::DrawingArea {
public:
    /// Status tier driving the left-border tint and the delta-arrow
    /// colour. Kept narrow on purpose: the dashboard's KPI cards live
    /// or die by an at-a-glance scan, and three buckets give the
    /// operator the same vocabulary as the existing QualityGauge.
    enum class Status : std::uint8_t {
        Ok       = 0,
        Warning  = 1,
        Critical = 2,
    };

    BigNumberCard();
    ~BigNumberCard() override = default;

    BigNumberCard(const BigNumberCard&)            = delete;
    BigNumberCard& operator=(const BigNumberCard&) = delete;
    BigNumberCard(BigNumberCard&&)                 = delete;
    BigNumberCard& operator=(BigNumberCard&&)      = delete;

    /// Metric name shown small at the top of the card. Already
    /// localised by the caller (no gettext done here).
    void setLabel(const Glib::ustring& label);

    /// Numeric value + decimal precision for the big middle row.
    /// Default 1 decimal place matches the existing pass-rate
    /// formatting in DashboardPage. Negative `decimals` is clamped
    /// to 0.
    void setValue(double value, int decimals = 1);

    /// Unit suffix rendered after the value at a smaller size
    /// ("%", "u/h", "ms"). Empty string is valid (no suffix drawn).
    void setUnit(const Glib::ustring& unit);

    /// Sets the target reference and turns on the bottom delta row.
    /// Pass any value (including 0.0) to enable the row. Use
    /// clearTarget() to hide it again.
    void setTarget(double target);

    /// Hide the delta row -- useful when the metric has no
    /// meaningful target (e.g. uptime counter).
    void clearTarget();

    /// Status tier drives the left-border tint and the delta-arrow
    /// colour. Defaults to Ok at construction.
    void setStatus(Status status);

    /// Accessors -- mostly for tests that want to assert on what
    /// the widget will paint next without rendering through Cairo.
    [[nodiscard]] const Glib::ustring& label()    const noexcept { return label_; }
    [[nodiscard]] double               value()    const noexcept { return value_; }
    [[nodiscard]] int                  decimals() const noexcept { return decimals_; }
    [[nodiscard]] const Glib::ustring& unit()     const noexcept { return unit_; }
    [[nodiscard]] bool                 hasTarget() const noexcept { return hasTarget_; }
    [[nodiscard]] double               target()   const noexcept { return target_; }
    [[nodiscard]] Status               status()   const noexcept { return status_; }

    /// Default minimum content size. Public so callers (and tests)
    /// can reason about layout budgets without inspecting CSS.
    static constexpr int kMinContentWidth  = 220;
    static constexpr int kMinContentHeight = 130;

private:
    void onDraw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    Glib::ustring label_;
    double        value_{0.0};
    int           decimals_{1};
    Glib::ustring unit_;
    double        target_{0.0};
    bool          hasTarget_{false};
    Status        status_{Status::Ok};
};

}  // namespace app::view
