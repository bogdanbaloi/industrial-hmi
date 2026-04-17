#pragma once

#include <gtkmm.h>
#include "src/gtk/view/colors.h"
#include "src/gtk/view/css_classes.h"
#include "src/gtk/view/ui_sizes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
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
class TrendChart : public Gtk::DrawingArea {
public:
    /// @param label  Chart title shown at the top-left.
    /// @param yMin   Minimum Y value (bottom of chart).
    /// @param yMax   Maximum Y value (top of chart).
    /// @param capacity  Number of data points to retain (width resolution).
    TrendChart(const std::string& label, float yMin, float yMax,
               std::size_t capacity = sizes::kTrendChartCapacity)
        : label_(label), yMin_(yMin), yMax_(yMax), capacity_(capacity) {
        data_.resize(capacity_, 0.0f);
        set_content_width(sizes::kTrendChartDefaultWidth);
        set_content_height(sizes::kTrendChartDefaultHeight);
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
    // Layout / styling constants.
    static constexpr double kPadTop         = 22.0;
    static constexpr double kPadBottom      = 4.0;
    static constexpr double kPadLeft        = 36.0;
    static constexpr double kPadRight       = 8.0;
    static constexpr double kGridLineWidth  = 0.5;
    static constexpr double kDataLineWidth  = 2.0;
    static constexpr double kGridAlpha      = 1.0;
    static constexpr int    kGridDivisions  = 4;
    static constexpr double kTitleFontSize  = 11.0;
    static constexpr double kAxisFontSize   = 10.0;
    static constexpr double kValueFontSize  = 12.0;
    static constexpr double kYLabelOffset   = 2.0;
    static constexpr double kYLabelBaseline = 3.0;
    static constexpr double kTitleBaseline  = 14.0;

    void onDraw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
        const double plotW = w - kPadLeft - kPadRight;
        const double plotH = h - kPadTop - kPadBottom;
        if (plotW <= 0 || plotH <= 0) return;

        const bool light = isLightMode();
        const Rgb bg   = light ? colors::kChartBgLightMode   : colors::kChartBgDarkMode;
        const Rgb grid = light ? colors::kChartGridLightMode : colors::kChartGridDarkMode;
        const Rgb fg   = light ? colors::kChartFgLightMode   : colors::kChartFgDarkMode;

        // Background
        cr->set_source_rgb(bg.r, bg.g, bg.b);
        cr->rectangle(0, 0, w, h);
        cr->fill();

        // Grid lines + Y labels
        cr->set_line_width(kGridLineWidth);
        cr->set_source_rgba(grid.r, grid.g, grid.b, kGridAlpha);
        cr->select_font_face("sans-serif", Cairo::ToyFontFace::Slant::NORMAL,
                             Cairo::ToyFontFace::Weight::NORMAL);
        cr->set_font_size(kAxisFontSize);

        for (int i = 0; i <= kGridDivisions; ++i) {
            const double frac = static_cast<double>(i) / kGridDivisions;
            const double y = kPadTop + plotH * (1.0 - frac);
            const float val = yMin_ + (yMax_ - yMin_) * static_cast<float>(frac);

            cr->move_to(kPadLeft, y);
            cr->line_to(kPadLeft + plotW, y);
            cr->stroke();

            const auto label = std::vformat("{:.0f}", std::make_format_args(val));
            cr->move_to(kYLabelOffset, y + kYLabelBaseline);
            cr->show_text(label);
        }

        // Title label (top-left)
        cr->set_source_rgb(fg.r, fg.g, fg.b);
        cr->set_font_size(kTitleFontSize);
        cr->move_to(kPadLeft, kTitleBaseline);
        cr->show_text(label_);

        // Data line
        if (count_ >= 2) {
            cr->set_line_width(kDataLineWidth);
            cr->set_source_rgb(colors::kTrendLine.r,
                               colors::kTrendLine.g,
                               colors::kTrendLine.b);
            cr->set_line_join(Cairo::Context::LineJoin::ROUND);

            bool first = true;
            for (std::size_t i = 0; i < count_; ++i) {
                const std::size_t idx = (writePos_ + capacity_ - count_ + i) % capacity_;
                const float val = std::clamp(data_[idx], yMin_, yMax_);

                const double x = kPadLeft + plotW * static_cast<double>(i) / (count_ - 1);
                const double frac = static_cast<double>(val - yMin_) / (yMax_ - yMin_);
                const double y = kPadTop + plotH * (1.0 - frac);

                if (first) {
                    cr->move_to(x, y);
                    first = false;
                } else {
                    cr->line_to(x, y);
                }
            }
            cr->stroke();
        }

        // Latest value text (top-right)
        if (count_ > 0) {
            const std::size_t lastIdx = (writePos_ + capacity_ - 1) % capacity_;
            const auto valText = std::vformat("{:.1f}%",
                                              std::make_format_args(data_[lastIdx]));

            cr->set_font_size(kValueFontSize);
            Cairo::TextExtents ext;
            cr->get_text_extents(valText, ext);
            cr->move_to(w - kPadRight - ext.width, kTitleBaseline);
            cr->show_text(valText);
        }
    }

    bool isLightMode() const {
        if (const auto* root = dynamic_cast<const Gtk::Window*>(get_root())) {
            return root->has_css_class(css::kLightMode);
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
