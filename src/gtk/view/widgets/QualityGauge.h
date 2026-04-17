#pragma once

#include <gtkmm.h>
#include "src/presenter/modelview/QualityCheckpointViewModel.h"
#include "src/gtk/view/colors.h"
#include "src/gtk/view/css_classes.h"
#include "src/gtk/view/ui_sizes.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace app::view {

/// Dynamic quality gauge widget.
///
/// Replaces the static SVG gauges: the arc length reflects the actual
/// pass-rate percentage (0-100) and the color is driven by the checkpoint
/// status (green/amber/red). The background track picks a theme-appropriate
/// shade by inspecting the toplevel window's `.light-mode` CSS class.
///
/// @usage
///   gauge_ = Gtk::make_managed<QualityGauge>();
///   gauge_->set_size_request(sizes::kGaugeSize, sizes::kGaugeSize);
///   ...
///   gauge_->setValue(98.5f, QualityCheckpointStatus::Passing);
class QualityGauge : public Gtk::DrawingArea {
public:
    QualityGauge() {
        set_content_width(sizes::kGaugeSize);
        set_content_height(sizes::kGaugeSize);
        set_draw_func(sigc::mem_fun(*this, &QualityGauge::onDraw));
    }

    void setValue(float passRate, presenter::QualityCheckpointStatus status) {
        passRate_ = std::clamp(passRate, 0.0f, kMaxPercent);
        status_ = status;
        queue_draw();
    }

private:
    // Layout constants - expressed here rather than as magic numbers below.
    static constexpr double kStroke     = 10.0;
    static constexpr double kMargin     = 6.0;
    static constexpr double kDotRadius  = 6.0;
    static constexpr float  kMaxPercent = 100.0f;

    void onDraw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
        const double cx = width / 2.0;
        const double cy = height / 2.0;
        const double radius = std::min(width, height) / 2.0 - kMargin;
        if (radius <= 0.0) return;

        const Rgb fg = statusColor();
        const Rgb bg = trackColor();

        // Background track (full ring)
        cr->set_source_rgb(bg.r, bg.g, bg.b);
        cr->set_line_width(kStroke);
        cr->set_line_cap(Cairo::Context::LineCap::BUTT);
        cr->arc(cx, cy, radius, 0.0, 2.0 * std::numbers::pi);
        cr->stroke();

        // Foreground arc proportional to pass rate, starting at 12 o'clock
        if (passRate_ > 0.0f) {
            const double start = -std::numbers::pi / 2.0;
            const double sweep = 2.0 * std::numbers::pi * (passRate_ / kMaxPercent);
            cr->set_source_rgb(fg.r, fg.g, fg.b);
            cr->set_line_width(kStroke);
            cr->set_line_cap(Cairo::Context::LineCap::ROUND);
            cr->arc(cx, cy, radius, start, start + sweep);
            cr->stroke();
        }

        // Center dot in the status color
        cr->set_source_rgb(fg.r, fg.g, fg.b);
        cr->arc(cx, cy, kDotRadius, 0.0, 2.0 * std::numbers::pi);
        cr->fill();
    }

    Rgb statusColor() const {
        switch (status_) {
            case presenter::QualityCheckpointStatus::Passing:  return colors::kStatusPassingGreen;
            case presenter::QualityCheckpointStatus::Warning:  return colors::kStatusWarningAmber;
            case presenter::QualityCheckpointStatus::Critical: return colors::kStatusCriticalRed;
        }
        return colors::kStatusPassingGreen;  // fallback if enum drifts
    }

    Rgb trackColor() const {
        if (const auto* root = dynamic_cast<const Gtk::Window*>(get_root())) {
            if (root->has_css_class(css::kLightMode)) {
                return colors::kTrackLightMode;
            }
        }
        return colors::kTrackDarkMode;
    }

    float passRate_{0.0f};
    presenter::QualityCheckpointStatus status_{
        presenter::QualityCheckpointStatus::Passing};
};

}  // namespace app::view
