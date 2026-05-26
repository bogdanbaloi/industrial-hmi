#include "src/gtk/view/widgets/BigNumberCard.h"

#include "src/gtk/view/colors.h"
#include "src/gtk/view/css_classes.h"

#include <pangomm.h>

#include <algorithm>
#include <format>
#include <string>

namespace app::view {

namespace {

// Layout constants -- all in widget-local pixels. Tuned for the
// kMinContentWidth/Height defaults but scale via the (width, height)
// passed to onDraw, which is what GTK actually allocates.
constexpr double kStripWidthPx       = 4.0;
constexpr double kCardPaddingPx      = 16.0;
constexpr double kRowGapPx           = 6.0;

constexpr int    kLabelFontPt        = 11;
constexpr int    kValueFontPt        = 38;
constexpr int    kUnitFontPt         = 20;
constexpr int    kDeltaFontPt        = 12;

// Text colours -- the value/label/delta share theme-aware shades so
// the card sits cleanly on light AND dark palettes. We pull the
// existing chart-fg colours (defined in colors.h) for consistency
// with TrendChart text rendering, then derive a slightly muted
// "secondary" tone for the label / delta-target half.
constexpr Rgb kLabelMutedDarkMode  = {0.62, 0.62, 0.66};
constexpr Rgb kLabelMutedLightMode = {0.40, 0.40, 0.44};

constexpr Rgb kValueDarkMode  = {0.95, 0.95, 0.97};
constexpr Rgb kValueLightMode = {0.12, 0.12, 0.15};

constexpr const char* kFontFamily = "Sans";

bool isDarkMode(Gtk::Widget& self) {
    if (const auto* root = dynamic_cast<const Gtk::Window*>(self.get_root())) {
        // The runtime palette layout toggles `.light-mode` on the
        // toplevel; absence means dark. Same convention as
        // QualityGauge / TrendChart.
        return !root->has_css_class(css::kLightMode);
    }
    return true;  // default to dark if not parented yet
}

Rgb statusRgb(BigNumberCard::Status status) {
    switch (status) {
        case BigNumberCard::Status::Ok:       return colors::kStatusPassingGreen;
        case BigNumberCard::Status::Warning:  return colors::kStatusWarningAmber;
        case BigNumberCard::Status::Critical: return colors::kStatusCriticalRed;
    }
    return colors::kStatusPassingGreen;
}

void setSource(const Cairo::RefPtr<Cairo::Context>& cr, const Rgb& c) {
    cr->set_source_rgb(c.r, c.g, c.b);
}

// Pango helper: draw `text` using the given font + size, with
// `(x, y)` as the TOP-LEFT corner. Returns the text's rendered
// width so the caller can stack the next item (e.g. the unit suffix
// after the value).
double drawText(const Cairo::RefPtr<Cairo::Context>& cr,
                const Glib::ustring&                 text,
                double                               x,
                double                               y,
                int                                  fontPt,
                bool                                 bold) {
    auto layout = Pango::Layout::create(cr);
    Pango::FontDescription fd(kFontFamily);
    fd.set_size(fontPt * Pango::SCALE);
    fd.set_weight(bold ? Pango::Weight::BOLD : Pango::Weight::NORMAL);
    layout->set_font_description(fd);
    layout->set_text(text);
    cr->move_to(x, y);
    layout->show_in_cairo_context(cr);
    int w = 0;
    int h = 0;
    layout->get_pixel_size(w, h);
    return static_cast<double>(w);
}

// Format the big number with the requested precision. Uses
// std::format so we don't drag in iostream / sprintf -- consistent
// with the rest of the codebase which prefers <format> on C++20.
std::string formatValue(double v, int decimals) {
    const int safe = std::max(0, decimals);
    return std::vformat("{:." + std::to_string(safe) + "f}",
                        std::make_format_args(v));
}

}  // namespace

BigNumberCard::BigNumberCard() {
    set_content_width(kMinContentWidth);
    set_content_height(kMinContentHeight);
    add_css_class("big-number-card");
    set_draw_func(sigc::mem_fun(*this, &BigNumberCard::onDraw));
}

void BigNumberCard::setLabel(const Glib::ustring& label) {
    if (label_ == label) return;
    label_ = label;
    queue_draw();
}

void BigNumberCard::setValue(double value, int decimals) {
    const int safeDecimals = std::max(0, decimals);
    if (value_ == value && decimals_ == safeDecimals) return;
    value_    = value;
    decimals_ = safeDecimals;
    queue_draw();
}

void BigNumberCard::setUnit(const Glib::ustring& unit) {
    if (unit_ == unit) return;
    unit_ = unit;
    queue_draw();
}

void BigNumberCard::setTarget(double target) {
    if (hasTarget_ && target_ == target) return;
    target_    = target;
    hasTarget_ = true;
    queue_draw();
}

void BigNumberCard::clearTarget() {
    if (!hasTarget_) return;
    hasTarget_ = false;
    target_    = 0.0;
    queue_draw();
}

void BigNumberCard::setStatus(Status status) {
    if (status_ == status) return;
    status_ = status;
    queue_draw();
}

void BigNumberCard::onDraw(const Cairo::RefPtr<Cairo::Context>& cr,
                           int                                  width,
                           int                                  height) {
    if (width <= 0 || height <= 0) return;

    const bool dark = isDarkMode(*this);
    const Rgb  label = dark ? kLabelMutedDarkMode : kLabelMutedLightMode;
    const Rgb  value = dark ? kValueDarkMode      : kValueLightMode;
    const Rgb  strip = statusRgb(status_);

    // Status strip on the left edge -- 4 px regardless of width so
    // the visual weight stays consistent across responsive layouts.
    setSource(cr, strip);
    cr->rectangle(0.0, 0.0, kStripWidthPx, static_cast<double>(height));
    cr->fill();

    const double xStart = kStripWidthPx + kCardPaddingPx;
    double y = kCardPaddingPx;

    // Row 1: label.
    if (!label_.empty()) {
        setSource(cr, label);
        drawText(cr, label_, xStart, y, kLabelFontPt, /*bold=*/false);
        y += static_cast<double>(kLabelFontPt) * 1.4 + kRowGapPx;
    }

    // Row 2: huge value + unit.
    {
        setSource(cr, value);
        const std::string formatted = formatValue(value_, decimals_);
        const double valueW =
            drawText(cr, formatted, xStart, y, kValueFontPt, /*bold=*/true);

        if (!unit_.empty()) {
            // Unit sits to the right of the value, baseline-aligned
            // visually by offsetting down so the smaller font appears
            // to share a baseline with the big value rather than
            // align at top.
            const double unitX = xStart + valueW + 6.0;
            const double unitY =
                y + static_cast<double>(kValueFontPt - kUnitFontPt);
            setSource(cr, label);
            drawText(cr, unit_, unitX, unitY, kUnitFontPt, /*bold=*/false);
        }
        y += static_cast<double>(kValueFontPt) * 1.25 + kRowGapPx;
    }

    // Row 3: delta vs target -- skipped when no target has been set.
    if (hasTarget_) {
        const double delta = value_ - target_;
        // "Up is good" for OEE / pass-rate / throughput, which is the
        // intended use. Arrow direction reflects sign of delta; arrow
        // colour reflects status (the dashboard decides Ok/Warning/
        // Critical based on its own thresholds, not just sign).
        const Glib::ustring arrow = delta >= 0.0
            ? Glib::ustring("\xe2\x96\xb2")   // U+25B2 BLACK UP-POINTING TRIANGLE
            : Glib::ustring("\xe2\x96\xbc");  // U+25BC BLACK DOWN-POINTING TRIANGLE

        const std::string deltaTxt =
            formatValue(delta >= 0.0 ? delta : -delta, decimals_);
        const std::string targetTxt = formatValue(target_, decimals_);

        setSource(cr, strip);  // arrow + magnitude in status colour
        double dx = xStart;
        dx += drawText(cr, arrow + " " + deltaTxt,
                       dx, y, kDeltaFontPt, /*bold=*/true);

        setSource(cr, label);
        drawText(cr,
                 Glib::ustring("  vs ") + targetTxt,
                 dx, y, kDeltaFontPt, /*bold=*/false);
    }
}

}  // namespace app::view
