#pragma once

#include "src/presenter/modelview/AlertViewModel.h"

#include <sigc++/signal.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace app::presenter {

/// AlertCenter — process-wide store for operator-visible alerts.
///
/// @design Header-only service (no virtual members, no external deps) so
///         test binaries can link it without pulling GTK or the MVP
///         layers. Presenter code injects a reference via DI rather than
///         grabbing a singleton.
///
///         Alerts are deduplicated by `key`: re-raising the same key
///         overwrites the previous entry's fields and timestamp, so a
///         quality checkpoint staying below threshold for many ticks
///         still occupies one row in the panel, not dozens.
///
/// @thread_safety Internal mutex; signal_alerts_changed fires on the
///                thread that called raise()/clear(). Views marshal to
///                the GTK main thread via Glib::signal_idle.
class AlertCenter {
public:
    /// Insert or replace an alert by its `key`. Updates timestamp to
    /// `now()` on every raise, even when the other fields are unchanged,
    /// so the panel can surface "still firing" conditions.
    void raise(const AlertViewModel& alert) {
        AlertViewModel stamped = alert;
        if (stamped.timestamp.empty()) {
            stamped.timestamp = formatNow();
        }
        {
            const std::scoped_lock lock(mutex_);
            auto it = std::find_if(alerts_.begin(), alerts_.end(),
                [&](const AlertViewModel& a) { return a.key == stamped.key; });
            if (it == alerts_.end()) {
                alerts_.push_back(std::move(stamped));
            } else {
                *it = std::move(stamped);
            }
        }
        signalAlertsChanged_.emit();
    }

    /// Drop the alert with the given key. No-op if none present. Emits
    /// signalAlertsChanged on success so the panel can refresh.
    void clear(std::string_view key) {
        bool removed = false;
        {
            const std::scoped_lock lock(mutex_);
            auto it = std::remove_if(alerts_.begin(), alerts_.end(),
                [&](const AlertViewModel& a) { return a.key == key; });
            if (it != alerts_.end()) {
                alerts_.erase(it, alerts_.end());
                removed = true;
            }
        }
        if (removed) signalAlertsChanged_.emit();
    }

    /// Drop every alert. Used by the "Clear all" button in the panel.
    void clearAll() {
        {
            const std::scoped_lock lock(mutex_);
            if (alerts_.empty()) return;
            alerts_.clear();
        }
        signalAlertsChanged_.emit();
    }

    /// Thread-safe snapshot — callers get a copy they can render without
    /// holding the lock. Cheap in practice; the list rarely exceeds a
    /// handful of entries.
    [[nodiscard]] std::vector<AlertViewModel> snapshot() const {
        const std::scoped_lock lock(mutex_);
        return alerts_;
    }

    [[nodiscard]] sigc::signal<void()>& signalAlertsChanged() {
        return signalAlertsChanged_;
    }

private:
    static std::string formatNow() {
        auto now = std::chrono::system_clock::now();
        auto tt  = std::chrono::system_clock::to_time_t(now);
        std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &tt);
#else
        localtime_r(&tt, &lt);
#endif
        std::ostringstream oss;
        oss << std::put_time(&lt, "%H:%M:%S");
        return oss.str();
    }

    mutable std::mutex          mutex_;
    std::vector<AlertViewModel> alerts_;
    sigc::signal<void()>        signalAlertsChanged_;
};

}  // namespace app::presenter
