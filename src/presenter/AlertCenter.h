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

/// History entry produced when an alarm is fully resolved (acknowledged
/// and returned to normal). The AlertViewModel is kept verbatim (title /
/// message / severity / last state) plus a `resolvedAt` wall-clock stamp.
struct AlertHistoryEntry {
    AlertViewModel alert;
    std::string    resolvedAt;
};

/// AlertCenter -- process-wide alarm store with an ISA-18.2-style
/// lifecycle (Phase 1 subset: UnackActive / AckActive / RtnUnack).
///
/// @design Header-only service (no virtual members, no external deps) so
///         test binaries can link it without pulling GTK or the MVP
///         layers. Presenter code injects a reference via DI rather than
///         grabbing a singleton.
///
///         Alarms are keyed by `key`. There are two distinct kinds of
///         input:
///           * PROCESS CONDITION (from the model/presenter):
///               raise(key)  -- the condition is active now
///               clear(key)  -- the condition returned to normal
///           * OPERATOR ACTION (from the UI):
///               acknowledge(key) -- the operator has seen the alarm
///
///         The lifecycle (the safety-relevant part):
///           absent + raise        -> UnackActive
///           UnackActive + ack     -> AckActive          (still visible)
///           UnackActive + clear   -> RtnUnack           (STILL VISIBLE)
///           AckActive   + clear   -> resolved (-> history, removed)
///           RtnUnack    + ack     -> resolved (-> history, removed)
///           RtnUnack    + raise   -> UnackActive         (re-alarm)
///         An unacknowledged condition that returns to normal is NOT
///         silently dropped -- the operator must acknowledge it first.
///
///         A bounded history ring (default cap = 50) records every
///         resolved alarm so operators can audit recent activity.
///
/// @thread_safety Internal mutex; signals fire on the thread that called
///                the mutator. Views marshal to the GTK main thread via
///                Glib::signal_idle.
class AlertCenter {
public:
    /// Maximum entries kept in the resolved-alarm history ring.
    static constexpr std::size_t kHistoryCapacity = 50;

    /// Report that an alarm's underlying condition is ACTIVE. Inserts a
    /// new UnackActive alarm, or refreshes an existing one's presentation
    /// fields + timestamp. A condition re-activating after it had returned
    /// to normal (RtnUnack) re-alarms (back to UnackActive); an already
    /// acknowledged-and-active alarm stays AckActive.
    void raise(const AlertViewModel& alert) {
        AlertViewModel stamped = alert;
        if (stamped.timestamp.empty()) {
            stamped.timestamp = formatNow();
        }
        {
            const std::scoped_lock lock(mutex_);
            auto it = findByKey(stamped.key);
            if (it == alarms_.end()) {
                stamped.state = AlarmState::UnackActive;
                alarms_.push_back(Alarm{std::move(stamped), /*active=*/true});
            } else {
                // Preserve the lifecycle state across a refresh, except a
                // returned alarm re-firing escalates back to unacked.
                AlarmState next = it->vm.state == AlarmState::RtnUnack
                                      ? AlarmState::UnackActive
                                      : it->vm.state;
                stamped.state        = next;
                it->vm               = std::move(stamped);
                it->conditionActive  = true;
            }
        }
        signalAlertsChanged_.emit();
    }

    /// Report that an alarm's underlying condition RETURNED TO NORMAL.
    /// If unacknowledged, the alarm becomes RtnUnack and stays visible;
    /// if already acknowledged, it is fully resolved (moved to history).
    /// No-op if the key is absent or already returned-to-normal.
    void clear(std::string_view key) {
        bool activeChanged  = false;
        bool historyChanged = false;
        {
            const std::scoped_lock lock(mutex_);
            auto it = findByKey(key);
            if (it == alarms_.end()) return;
            it->conditionActive = false;
            if (it->vm.state == AlarmState::AckActive) {
                pushHistoryUnlocked(it->vm);
                alarms_.erase(it);
                activeChanged = historyChanged = true;
            } else if (it->vm.state == AlarmState::UnackActive) {
                it->vm.state  = AlarmState::RtnUnack;
                activeChanged = true;
            }
            // RtnUnack + clear -> nothing changes (still awaiting ack).
        }
        if (activeChanged)  signalAlertsChanged_.emit();
        if (historyChanged) signalHistoryChanged_.emit();
    }

    /// Operator acknowledges an alarm. An active alarm becomes AckActive
    /// (stays visible until its condition clears); an alarm that already
    /// returned to normal (RtnUnack) is fully resolved (moved to history).
    /// No-op if the key is absent or already acknowledged-and-active.
    void acknowledge(std::string_view key) {
        bool activeChanged  = false;
        bool historyChanged = false;
        {
            const std::scoped_lock lock(mutex_);
            auto it = findByKey(key);
            if (it == alarms_.end()) return;
            if (it->vm.state == AlarmState::UnackActive) {
                it->vm.state  = AlarmState::AckActive;
                activeChanged = true;
            } else if (it->vm.state == AlarmState::RtnUnack) {
                pushHistoryUnlocked(it->vm);
                alarms_.erase(it);
                activeChanged = historyChanged = true;
            }
        }
        if (activeChanged)  signalAlertsChanged_.emit();
        if (historyChanged) signalHistoryChanged_.emit();
    }

    /// Operator force-resolves every alarm regardless of state (the
    /// "Clear all" button). Each is appended to history.
    void clearAll() {
        bool historyChanged = false;
        {
            const std::scoped_lock lock(mutex_);
            if (alarms_.empty()) return;
            for (const auto& alarm : alarms_) {
                pushHistoryUnlocked(alarm.vm);
                historyChanged = true;
            }
            alarms_.clear();
        }
        signalAlertsChanged_.emit();
        if (historyChanged) signalHistoryChanged_.emit();
    }

    /// Thread-safe snapshot of the active alarms (any non-resolved state),
    /// projected to view models the panel can render directly.
    [[nodiscard]] std::vector<AlertViewModel> snapshot() const {
        const std::scoped_lock lock(mutex_);
        std::vector<AlertViewModel> out;
        out.reserve(alarms_.size());
        for (const auto& alarm : alarms_) {
            out.push_back(alarm.vm);
        }
        return out;
    }

    /// Snapshot of the resolved-alarm history, newest-first. Bounded by
    /// kHistoryCapacity (oldest entries drop off when the ring fills).
    [[nodiscard]] std::vector<AlertHistoryEntry> history() const {
        const std::scoped_lock lock(mutex_);
        return history_;
    }

    /// Wipe the history ring. Does not affect active alarms.
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

    /// Re-run each alarm's `retranslate` callback (when set), both for
    /// active alarms and history. Used by MainWindow after a live
    /// language switch so rows re-render in the new locale without
    /// waiting for the next model tick. No-op for alarms whose producer
    /// didn't install a retranslate callback.
    void retranslate() {
        bool activeAny  = false;
        bool historyAny = false;
        {
            const std::scoped_lock lock(mutex_);
            for (auto& alarm : alarms_) {
                if (alarm.vm.retranslate) {
                    alarm.vm.retranslate(alarm.vm);
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
    /// One tracked alarm: its presentation view model (carrying the
    /// lifecycle state) plus whether the underlying condition is active.
    struct Alarm {
        AlertViewModel vm;
        bool           conditionActive{true};
    };

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

    // Find an alarm by key. Caller must hold mutex_.
    std::vector<Alarm>::iterator findByKey(std::string_view key) {
        return std::find_if(alarms_.begin(), alarms_.end(),
            [&](const Alarm& a) { return a.vm.key == key; });
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
    std::vector<Alarm>              alarms_;
    std::vector<AlertHistoryEntry>  history_;
    sigc::signal<void()>            signalAlertsChanged_;
    sigc::signal<void()>            signalHistoryChanged_;
};

}  // namespace app::presenter
