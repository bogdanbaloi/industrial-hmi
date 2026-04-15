#pragma once

#include <gtkmm.h>
#include "src/presenter/modelview/QualityCheckpointViewModel.h"

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
///   gauge_->set_size_request(100, 100);
///   ...
///   gauge_->setValue(98.5f, QualityCheckpointStatus::Passing);
class QualityGauge : public Gtk::DrawingArea {
public:
    QualityGauge() {
        // GTK4 DrawingArea needs explicit content size so its measure() reports
        // consistent natural dimensions; otherwise parent boxes emit warnings
        // about min height > natural height.
        set_content_width(100);
        set_content_height(100);
        set_draw_func(sigc::mem_fun(*this, &QualityGauge::onDraw));
    }

    void setValue(float passRate, presenter::QualityCheckpointStatus status) {
        passRate_ = std::clamp(passRate, 0.0f, 100.0f);
        status_ = status;
        queue_draw();
    }

private:
    void onDraw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
        constexpr double kStroke = 10.0;
        constexpr double kMargin = 6.0;
        constexpr double kDotRadius = 6.0;

        const double cx = width / 2.0;
        const double cy = height / 2.0;
        const double radius = std::min(width, height) / 2.0 - kMargin;
        if (radius <= 0.0) return;

        double fgR, fgG, fgB;
        statusColor(fgR, fgG, fgB);

        double bgR, bgG, bgB;
        trackColor(bgR, bgG, bgB);

        // Background track (full ring)
        cr->set_source_rgb(bgR, bgG, bgB);
        cr->set_line_width(kStroke);
        cr->set_line_cap(Cairo::Context::LineCap::BUTT);
        cr->arc(cx, cy, radius, 0.0, 2.0 * std::numbers::pi);
        cr->stroke();

        // Foreground arc proportional to pass rate, starting at 12 o'clock
        if (passRate_ > 0.0f) {
            const double start = -std::numbers::pi / 2.0;
            const double sweep = 2.0 * std::numbers::pi * (passRate_ / 100.0);
            cr->set_source_rgb(fgR, fgG, fgB);
            cr->set_line_width(kStroke);
            cr->set_line_cap(Cairo::Context::LineCap::ROUND);
            cr->arc(cx, cy, radius, start, start + sweep);
            cr->stroke();
        }

        // Center dot in the status color
        cr->set_source_rgb(fgR, fgG, fgB);
        cr->arc(cx, cy, kDotRadius, 0.0, 2.0 * std::numbers::pi);
        cr->fill();
    }

    void statusColor(double& r, double& g, double& b) const {
        switch (status_) {
            case presenter::QualityCheckpointStatus::Passing:
                r = 0x4C / 255.0; g = 0xAF / 255.0; b = 0x50 / 255.0;
                break;
            case presenter::QualityCheckpointStatus::Warning:
                r = 0xFF / 255.0; g = 0x98 / 255.0; b = 0x00 / 255.0;
                break;
            case presenter::QualityCheckpointStatus::Critical:
                r = 0xF4 / 255.0; g = 0x43 / 255.0; b = 0x36 / 255.0;
                break;
        }
    }

    // Track color adapts to theme: light mode uses a soft gray, dark mode uses
    // a slightly lifted near-black so the ring is still visible on card bg.
    void trackColor(double& r, double& g, double& b) const {
        if (const auto* root = dynamic_cast<const Gtk::Window*>(get_root())) {
            if (root->has_css_class("light-mode")) {
                r = 0xE0 / 255.0; g = 0xE0 / 255.0; b = 0xE0 / 255.0;
                return;
            }
        }
        r = 0x3A / 255.0; g = 0x3A / 255.0; b = 0x3A / 255.0;
    }

    float passRate_{0.0f};
    presenter::QualityCheckpointStatus status_{
        presenter::QualityCheckpointStatus::Passing};
};

}  // namespace app::view
