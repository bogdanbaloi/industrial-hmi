#pragma once

#include "src/core/i18n.h"

#include <gtkmm.h>
#include <glibmm/main.h>

#include <array>
#include <chrono>
#include <ctime>
#include <format>

namespace app::view {

/// Two-line clock + date label for the sidebar footer, refreshed once
/// per second. Pure view — no DI needed, just set up a
/// `Glib::signal_timeout` that updates the text.
class LiveClock : public Gtk::Box {
public:
    LiveClock() : Gtk::Box(Gtk::Orientation::VERTICAL, 2) {
        add_css_class("live-clock");
        set_margin_start(20);
        set_margin_end(20);

        time_ = Gtk::make_managed<Gtk::Label>();
        time_->add_css_class("live-clock-time");
        time_->set_xalign(0);
        append(*time_);

        date_ = Gtk::make_managed<Gtk::Label>();
        date_->add_css_class("live-clock-date");
        date_->set_xalign(0);
        append(*date_);

        update();
        timer_ = Glib::signal_timeout().connect_seconds(
            [this]() { update(); return true; }, 1);
    }

    ~LiveClock() override { timer_.disconnect(); }

    /// Re-render immediately from the currently active gettext catalog.
    /// Called by MainWindow after a live language switch so the weekday
    /// and month abbreviations update without waiting for the next
    /// 1-second timer tick.
    void refreshTranslations() { update(); }

    LiveClock(const LiveClock&) = delete;
    LiveClock& operator=(const LiveClock&) = delete;
    LiveClock(LiveClock&&) = delete;
    LiveClock& operator=(LiveClock&&) = delete;

private:
    void update() {
        auto now = std::chrono::system_clock::now();
        auto tt  = std::chrono::system_clock::to_time_t(now);
        std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &tt);
#else
        localtime_r(&tt, &lt);
#endif

        // Weekday + month abbreviations via our own gettext catalog
        // rather than strftime — Windows CRT's LC_TIME doesn't follow
        // LANGUAGE reliably, while our .mo files always match the
        // currently selected locale.
        static constexpr std::array<const char*, 7> kDays{
            "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static constexpr std::array<const char*, 12> kMonths{
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

        time_->set_label(std::format("{:02}:{:02}:{:02}",
                                     lt.tm_hour, lt.tm_min, lt.tm_sec));
        date_->set_label(std::format("{}, {} {}",
                                     _(kDays[static_cast<std::size_t>(lt.tm_wday)]),
                                     _(kMonths[static_cast<std::size_t>(lt.tm_mon)]),
                                     lt.tm_mday));
    }

    Gtk::Label*      time_{nullptr};
    Gtk::Label*      date_{nullptr};
    sigc::connection timer_;
};

}  // namespace app::view
