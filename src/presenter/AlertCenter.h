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

/// History entry produced when an active alert is cleared. The original
/// AlertViewModel is kept verbatim (title / message / severity as last
/// seen) plus a `resolvedAt` wall-clock stamp.
struct AlertHistoryEntry {
    AlertViewModel alert;
    std::string    resolvedAt;
};

/// AlertCenter -- process-wide store for operator-visible alerts.
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
///         A bounded history ring (default cap = 50) records every
///         cleared alert so operators can audit recent activity even
///         after the condition has resolved. Oldest entries are dropped
///         when the ring fills up.
///
/// @thread_safety Internal mutex; signal_alerts_changed fires on the
///                thread that called raise()/clear(). Views marshal to
///                the GTK main thread via Glib::signal_idle.
class AlertCenter {
public:
    /// Maximum entries kept in the resolved-alert history ring.
    static constexpr std::size_t kHistoryCapacity = 50;

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
    /// signalAlertsChanged on success so the panel can refresh. Cleared
    /// alerts are pushed to history (newest-first) for audit.
    void clear(std::string_view key) {
        bool activeChanged = false;
        bool historyChanged = false;
        {
            const std::scoped_lock lock(mutex_);
            auto it = std::find_if(alerts_.begin(), alerts_.end(),
                [&](const AlertViewModel& a) { return a.key == key; });
            if (it != alerts_.end()) {
                pushHistoryUnlocked(*it);
                alerts_.erase(it);
                activeChanged = true;
                historyChanged = true;
            }
        }
        if (activeChanged)  signalAlertsChanged_.emit();
        if (historyChanged) signalHistoryChanged_.emit();
    }

    /// Drop every alert. Used by the "Clear all" button in the panel.
    /// Each dropped alert is appended to history.
    void clearAll() {
        bool historyChanged = false;
        {
            const std::scoped_lock lock(mutex_);
            if (alerts_.empty()) return;
            for (const auto& a : alerts_) {
                pushHistoryUnlocked(a);
                historyChanged = true;
            }
            alerts_.clear();
        }
        signalAlertsChanged_.emit();
        if (historyChanged) signalHistoryChanged_.emit();
    }

    /// Thread-safe snapshot -- callers get a copy they can render without
    /// holding the lock. Cheap in practice; the list rarely exceeds a
    /// handful of entries.
    [[nodiscard]] std::vector<AlertViewModel> snapshot() const {
        const std::scoped_lock lock(mutex_);
        return alerts_;
    }

    /// Snapshot of the cleared-alert history, newest-first. Bounded by
    /// kHistoryCapacity (oldest entries drop off when the ring fills).
    [[nodiscard]] std::vector<AlertHistoryEntry> history() const {
        const std::scoped_lock lock(mutex_);
        return history_;
    }

    /// Wipe the history ring. Does not affect active alerts.
    void clearHistory() {
        {
            const std::scoped_lock lock(mutex_);
            if (history_.empty()) return;
            history_.clear();
        }
        signalHistoryChanged_.emit();
    }

    [[nodiscard]] sigc::signal<void()>& signalAlertsChanged() {
        return signalAlertsChanged_;
    }

    [[nodiscard]] sigc::signal<void()>& signalHistoryChanged() {
        return signalHistoryChanged_;
    }

    /// Re-run each alert's `retranslate` callback (when set), both for
    /// active alerts and history. Used by MainWindow after a live
    /// language switch so rows re-render in the new locale without
    /// waiting for the next model tick. No-op for alerts whose producer
    /// didn't install a retranslate callback.
    void retranslate() {
        bool activeAny  = false;
        bool historyAny = false;
        {
            const std::scoped_lock lock(mutex_);
            for (auto& a : alerts_) {
                if (a.retranslate) {
                    a.retranslate(a);
                    activeAny = true;
                }
            }
            for (auto& entry : history_) {
                if (entry.alert.retranslate) {
                    entry.alert.retranslate(entry.alert);
                    historyAny = true;
                }
            }
        }
        if (activeAny)  signalAlertsChanged_.emit();
        if (historyAny) signalHistoryChanged_.emit();
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

    // Push an entry to the newest-first history ring, discarding the
    // oldest when capacity is reached. Caller must hold mutex_.
    void pushHistoryUnlocked(const AlertViewModel& alert) {
        AlertHistoryEntry entry{alert, formatNow()};
        history_.insert(history_.begin(), std::move(entry));
        if (history_.size() > kHistoryCapacity) {
            history_.pop_back();
        }
    }

    mutable std::mutex              mutex_;
    std::vector<AlertViewModel>     alerts_;
    std::vector<AlertHistoryEntry>  history_;
    sigc::signal<void()>            signalAlertsChanged_;
    sigc::signal<void()>            signalHistoryChanged_;
};

}  // namespace app::presenter
