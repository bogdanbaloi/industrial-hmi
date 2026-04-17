#pragma once

#include <gtkmm.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>

namespace app::view {

/// Real-time trend line chart drawn with Cairo.
///
/// Maintains a circular buffer of the last N data points and redraws
/// on every addPoint() call. The Y axis auto-scales to the configured
/// min/max range, and horizontal grid lines are drawn at regular
/// intervals for readability.
///
/// Theme-aware: reads the toplevel window's "light-mode" CSS class to
/// pick appropriate foreground/background colors.
///
/// Usage:
///   auto* chart = Gtk::make_managed<TrendChart>("Pass Rate", 80.0, 100.0, 60);
///   chart->addPoint(98.5f);   // call every tick
class TrendChart : public Gtk::DrawingArea {
public:
    /// @param label  Chart title shown at the top-left.
    /// @param yMin   Minimum Y value (bottom of chart).
    /// @param yMax   Maximum Y value (top of chart).
    /// @param capacity  Number of data points to retain (width resolution).
    TrendChart(const std::string& label, float yMin, float yMax,
               std::size_t capacity = 60)
        : label_(label), yMin_(yMin), yMax_(yMax), capacity_(capacity) {
        data_.resize(capacity_, 0.0f);
        set_content_width(300);
        set_content_height(80);
        set_draw_func(sigc::mem_fun(*this, &TrendChart::onDraw));
    }

    /// Append a new value and trigger a redraw.
    void addPoint(float value) {
        data_[writePos_] = value;
        writePos_ = (writePos_ + 1) % capacity_;
        if (count_ < capacity_) ++count_;
        queue_draw();
    }

private:
    void onDraw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
        constexpr double kPadTop = 22.0;
        constexpr double kPadBottom = 4.0;
        constexpr double kPadLeft = 36.0;
        constexpr double kPadRight = 8.0;

        const double plotW = w - kPadLeft - kPadRight;
        const double plotH = h - kPadTop - kPadBottom;
        if (plotW <= 0 || plotH <= 0) return;

        const bool light = isLightMode();

        // Background
        cr->set_source_rgb(light ? 0.98 : 0.15,
                           light ? 0.98 : 0.15,
                           light ? 0.98 : 0.17);
        cr->rectangle(0, 0, w, h);
        cr->fill();

        // Grid lines + Y labels (4 divisions)
        cr->set_line_width(0.5);
        cr->set_source_rgba(light ? 0.7 : 0.35,
                            light ? 0.7 : 0.35,
                            light ? 0.7 : 0.38, 1.0);
        cr->select_font_face("sans-serif", Cairo::ToyFontFace::Slant::NORMAL,
                             Cairo::ToyFontFace::Weight::NORMAL);
        cr->set_font_size(10);

        constexpr int kGridLines = 4;
        for (int i = 0; i <= kGridLines; ++i) {
            double frac = static_cast<double>(i) / kGridLines;
            double y = kPadTop + plotH * (1.0 - frac);
            float val = yMin_ + (yMax_ - yMin_) * static_cast<float>(frac);

            cr->move_to(kPadLeft, y);
            cr->line_to(kPadLeft + plotW, y);
            cr->stroke();

            char buf[8];
            std::snprintf(buf, sizeof(buf), "%.0f", val);
            cr->move_to(2, y + 3);
            cr->show_text(buf);
        }

        // Title label
        cr->set_source_rgb(light ? 0.2 : 0.85,
                           light ? 0.2 : 0.85,
                           light ? 0.2 : 0.88);
        cr->set_font_size(11);
        cr->move_to(kPadLeft, 14);
        cr->show_text(label_);

        // Data line
        if (count_ < 2) return;

        cr->set_line_width(2.0);
        cr->set_source_rgb(0.30, 0.69, 0.31);  // green #4CAF50
        cr->set_line_join(Cairo::Context::LineJoin::ROUND);

        bool first = true;
        for (std::size_t i = 0; i < count_; ++i) {
            // Read from oldest to newest
            std::size_t idx = (writePos_ + capacity_ - count_ + i) % capacity_;
            float val = std::clamp(data_[idx], yMin_, yMax_);

            double x = kPadLeft + plotW * static_cast<double>(i) / (count_ - 1);
            double frac = static_cast<double>(val - yMin_) / (yMax_ - yMin_);
            double y = kPadTop + plotH * (1.0 - frac);

            if (first) {
                cr->move_to(x, y);
                first = false;
            } else {
                cr->line_to(x, y);
            }
        }
        cr->stroke();

        // Latest value text (top-right)
        if (count_ > 0) {
            std::size_t lastIdx = (writePos_ + capacity_ - 1) % capacity_;
            char valBuf[16];
            std::snprintf(valBuf, sizeof(valBuf), "%.1f%%", data_[lastIdx]);

            cr->set_font_size(12);
            Cairo::TextExtents ext;
            cr->get_text_extents(valBuf, ext);
            cr->move_to(w - kPadRight - ext.width, 14);
            cr->show_text(valBuf);
        }
    }

    bool isLightMode() const {
        if (const auto* root = dynamic_cast<const Gtk::Window*>(get_root())) {
            return root->has_css_class("light-mode");
        }
        return false;
    }

    std::string label_;
    float yMin_;
    float yMax_;
    std::size_t capacity_;

    std::vector<float> data_;
    std::size_t writePos_{0};
    std::size_t count_{0};
};

}  // namespace app::view
