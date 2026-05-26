#include "src/gtk/view/widgets/DonutChartWidget.h"

#include "src/gtk/view/css_classes.h"

#include <pangomm.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace app::view {

namespace {

// Layout constants -- all derived from the smaller of the (width,
// height) passed to onDraw so the widget scales cleanly when the
// dashboard hands it a non-default slot. Ratios picked so the ring
// reads as a donut (not a thin annulus) at the kMinContentWidth /
// Height defaults.
constexpr double kMarginRatio        = 0.06;   // outer margin
constexpr double kRingThicknessRatio = 0.18;   // ring width relative to radius

// Centre font sizes scale with the widget's shorter dimension so
// the headline number reads at any donut size from compact inline
// (110 px) to dedicated section (200 px+). Pivot dimension picked
// at the default kMinContentWidth -- the donut renders as designed
// at that size, then scales proportionally up or down.
constexpr double kCenterTitleFontRatio    = 0.14;   // 200px -> 28pt
constexpr double kCenterSubtitleFontRatio = 0.055;  // 200px -> 11pt
constexpr int    kCenterTitleFontPtMin    = 12;
constexpr int    kCenterSubtitleFontPtMin = 8;
constexpr double kCenterLabelGapPx        = 4.0;

constexpr Rgb kCenterTitleDarkMode  = {0.95, 0.95, 0.97};
constexpr Rgb kCenterTitleLightMode = {0.12, 0.12, 0.15};

constexpr Rgb kCenterSubMutedDarkMode  = {0.62, 0.62, 0.66};
constexpr Rgb kCenterSubMutedLightMode = {0.40, 0.40, 0.44};

constexpr const char* kFontFamily = "Sans";

bool isDarkMode(Gtk::Widget& self) {
    if (const auto* root = dynamic_cast<const Gtk::Window*>(self.get_root())) {
        return !root->has_css_class(css::kLightMode);
    }
    return true;
}

void setSource(const Cairo::RefPtr<Cairo::Context>& cr, const Rgb& c) {
    cr->set_source_rgb(c.r, c.g, c.b);
}

// Pango helper: render `text` centred horizontally on `cx`, with
// the TOP edge at `y`. Returns the rendered height so the caller
// can stack the subtitle below the title.
double drawCenteredText(const Cairo::RefPtr<Cairo::Context>& cr,
                        const Glib::ustring&                 text,
                        double                               cx,
                        double                               y,
                        int                                  fontPt,
                        bool                                 bold) {
    auto layout = Pango::Layout::create(cr);
    Pango::FontDescription fd(kFontFamily);
    fd.set_size(fontPt * Pango::SCALE);
    fd.set_weight(bold ? Pango::Weight::BOLD : Pango::Weight::NORMAL);
    layout->set_font_description(fd);
    layout->set_text(text);
    int w = 0;
    int h = 0;
    layout->get_pixel_size(w, h);
    cr->move_to(cx - static_cast<double>(w) / 2.0, y);
    layout->show_in_cairo_context(cr);
    return static_cast<double>(h);
}

}  // namespace

DonutChartWidget::DonutChartWidget() {
    set_content_width(kMinContentWidth);
    set_content_height(kMinContentHeight);
    add_css_class("donut-chart");
    set_draw_func(sigc::mem_fun(*this, &DonutChartWidget::onDraw));
}

void DonutChartWidget::setSegments(std::vector<Segment> segments) {
    segments_ = std::move(segments);
    queue_draw();
}

void DonutChartWidget::setCenterTitle(const Glib::ustring& text) {
    if (centerTitle_ == text) return;
    centerTitle_ = text;
    queue_draw();
}

void DonutChartWidget::setCenterSubtitle(const Glib::ustring& text) {
    if (centerSubtitle_ == text) return;
    centerSubtitle_ = text;
    queue_draw();
}

void DonutChartWidget::onDraw(const Cairo::RefPtr<Cairo::Context>& cr,
                              int                                  width,
                              int                                  height) {
    if (width <= 0 || height <= 0) return;

    const double cx     = static_cast<double>(width) / 2.0;
    const double cy     = static_cast<double>(height) / 2.0;
    const auto shorter = static_cast<double>(std::min(width, height));
    const double margin = shorter * kMarginRatio;
    const double outerRadius = (shorter / 2.0) - margin;
    if (outerRadius <= 0.0) return;
    const double ringThickness = outerRadius * kRingThicknessRatio;

    // Normalise segment values. Negative or zero values collapse
    // the segment so the widget tolerates messy input from the
    // dashboard (e.g. a state with no time spent in it yet).
    double total = 0.0;
    for (const auto& s : segments_) {
        if (s.value > 0.0) total += s.value;
    }

    if (total > 0.0) {
        constexpr double kFullTurn = 2.0 * std::numbers::pi;
        constexpr double kQuarter  = std::numbers::pi / 2.0;
        double start = -kQuarter;  // 12 o'clock
        cr->set_line_width(ringThickness);
        cr->set_line_cap(Cairo::Context::LineCap::BUTT);
        for (const auto& s : segments_) {
            if (s.value <= 0.0) continue;
            const double sweep = kFullTurn * (s.value / total);
            setSource(cr, s.color);
            cr->arc(cx, cy, outerRadius - ringThickness / 2.0,
                    start, start + sweep);
            cr->stroke();
            start += sweep;
        }
    }

    // Centre labels. Computed after the ring so they paint on top
    // (Cairo's last-write-wins inside the disk hole). Stacked with
    // a small gap; if only one is set, it renders centred on cy.
    const bool dark = isDarkMode(*this);
    const Rgb  titleColor = dark ? kCenterTitleDarkMode : kCenterTitleLightMode;
    const Rgb  subColor   = dark ? kCenterSubMutedDarkMode : kCenterSubMutedLightMode;

    // Scale fonts with the widget's shorter dimension so the donut
    // reads correctly at both inline-compact (110 px) and dedicated-
    // section (200 px+) sizes. Floor at the *Min constants so very
    // small slots still render legible text.
    constexpr double kLineHeightFactor = 1.2;
    const int titleFontPt = std::max(
        kCenterTitleFontPtMin,
        static_cast<int>(shorter * kCenterTitleFontRatio));
    const int subFontPt   = std::max(
        kCenterSubtitleFontPtMin,
        static_cast<int>(shorter * kCenterSubtitleFontRatio));

    if (!centerTitle_.empty() && !centerSubtitle_.empty()) {
        const double titleHeight =
            static_cast<double>(titleFontPt) * kLineHeightFactor;
        const double subHeight =
            static_cast<double>(subFontPt) * kLineHeightFactor;
        const double blockHeight =
            titleHeight + kCenterLabelGapPx + subHeight;
        double y = cy - blockHeight / 2.0;
        setSource(cr, titleColor);
        const double drawnTitle =
            drawCenteredText(cr, centerTitle_, cx, y,
                             titleFontPt, /*bold=*/true);
        y += drawnTitle + kCenterLabelGapPx;
        setSource(cr, subColor);
        drawCenteredText(cr, centerSubtitle_, cx, y,
                         subFontPt, /*bold=*/false);
    } else if (!centerTitle_.empty()) {
        const double titleHeight =
            static_cast<double>(titleFontPt) * kLineHeightFactor;
        const double y = cy - titleHeight / 2.0;
        setSource(cr, titleColor);
        drawCenteredText(cr, centerTitle_, cx, y,
                         titleFontPt, /*bold=*/true);
    } else if (!centerSubtitle_.empty()) {
        const double subHeight =
            static_cast<double>(subFontPt) * kLineHeightFactor;
        const double y = cy - subHeight / 2.0;
        setSource(cr, subColor);
        drawCenteredText(cr, centerSubtitle_, cx, y,
                         subFontPt, /*bold=*/false);
    }
}

}  // namespace app::view
