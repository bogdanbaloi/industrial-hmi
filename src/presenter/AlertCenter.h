#pragma once

#include "src/presenter/modelview/AlertViewModel.h"

#include <sigc++/signal.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <functional>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
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

    /// Wall-clock type used for shelve deadlines. Steady_clock would be
    /// more correct for durations (monotonic), but we display deadlines
    /// to the operator + carry them across log rotations -- system_clock
    /// matches the existing AlertViewModel::timestamp convention.
    using Clock     = std::chrono::system_clock;
    using TimePoint = Clock::time_point;
    using NowFn     = std::function<TimePoint()>;

    /// Default ctor wires the real wall clock.
    AlertCenter()
        : now_([] { return Clock::now(); }) {}

    /// DI ctor for tests: pass a controlled clock so shelve auto-expiry
    /// cases are deterministic without sleeping.
    explicit AlertCenter(NowFn now) : now_(std::move(now)) {}

    /// Audit hook (REQ-ALARM-004). When set, every lifecycle transition
    /// (raise/ack/rtn/resolve/shelve/unshelve/expire/re-alarm) invokes
    /// the callback. AlertCenter stays decoupled from auth/* -- the
    /// composition root (MainWindow) wires a lambda that captures the
    /// AuditLogger + Session and translates into an AuditEvent with the
    /// `category::kAlert` bucket. Optional: tests + headless builds can
    /// leave it unset and lifecycle events are not journalled.
    using AuditFn = std::function<void(std::string_view action,
                                       std::string_view details)>;
    void setAuditCallback(AuditFn fn) { auditFn_ = std::move(fn); }

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
        // Capture the key before potentially moving stamped into storage,
        // so the post-lock audit hook can still see it.
        const std::string key = stamped.key;
        std::string_view auditAction;  // empty -> no audit emit
        {
            const std::scoped_lock lock(mutex_);
            auto it = findByKey(stamped.key);
            if (it == alarms_.end()) {
                stamped.state = AlarmState::UnackActive;
                alarms_.push_back(Alarm{std::move(stamped), /*active=*/true});
                auditAction = "RAISE";
            } else {
                // Preserve the lifecycle state across a refresh, except a
                // returned alarm re-firing escalates back to unacked.
                const bool wasReturned =
                    it->vm.state == AlarmState::RtnUnack;
                AlarmState next = wasReturned ? AlarmState::UnackActive
                                              : it->vm.state;
                stamped.state        = next;
                it->vm               = std::move(stamped);
                it->conditionActive  = true;
                if (wasReturned) auditAction = "REALARM";
                // else: a refresh of an existing active alarm is not a
                // lifecycle transition and is intentionally NOT audited.
            }
        }
        signalAlertsChanged_.emit();
        if (!auditAction.empty()) emitAudit(auditAction, key);
    }

    /// Report that an alarm's underlying condition RETURNED TO NORMAL.
    /// If unacknowledged, the alarm becomes RtnUnack and stays visible;
    /// if already acknowledged, it is fully resolved (moved to history).
    /// No-op if the key is absent or already returned-to-normal.
    void clear(std::string_view key) {
        bool activeChanged  = false;
        bool historyChanged = false;
        std::string_view auditAction;
        const std::string keyCopy(key);
        {
            const std::scoped_lock lock(mutex_);
            auto it = findByKey(key);
            if (it == alarms_.end()) return;
            it->conditionActive = false;
            if (it->vm.state == AlarmState::AckActive) {
                pushHistoryUnlocked(it->vm);
                alarms_.erase(it);
                activeChanged = historyChanged = true;
                auditAction = "RESOLVE";
            } else if (it->vm.state == AlarmState::UnackActive) {
                it->vm.state  = AlarmState::RtnUnack;
                activeChanged = true;
                auditAction = "RTN";
            }
            // RtnUnack / Shelved + clear -> conditionActive updated above
            // but no visible state change; intentionally NOT audited.
        }
        if (activeChanged)  signalAlertsChanged_.emit();
        if (historyChanged) signalHistoryChanged_.emit();
        if (!auditAction.empty()) emitAudit(auditAction, keyCopy);
    }

    /// Operator acknowledges an alarm. An active alarm becomes AckActive
    /// (stays visible until its condition clears); an alarm that already
    /// returned to normal (RtnUnack) is fully resolved (moved to history).
    /// No-op if the key is absent or already acknowledged-and-active.
    void acknowledge(std::string_view key) {
        bool activeChanged  = false;
        bool historyChanged = false;
        std::string_view auditAction;
        const std::string keyCopy(key);
        {
            const std::scoped_lock lock(mutex_);
            auto it = findByKey(key);
            if (it == alarms_.end()) return;
            if (it->vm.state == AlarmState::UnackActive) {
                it->vm.state  = AlarmState::AckActive;
                activeChanged = true;
                auditAction = "ACK";
            } else if (it->vm.state == AlarmState::RtnUnack) {
                pushHistoryUnlocked(it->vm);
                alarms_.erase(it);
                activeChanged = historyChanged = true;
                auditAction = "RESOLVE";
            }
        }
        if (activeChanged)  signalAlertsChanged_.emit();
        if (historyChanged) signalHistoryChanged_.emit();
        if (!auditAction.empty()) emitAudit(auditAction, keyCopy);
    }

    /// Operator temporarily suppresses an alarm for `duration`. The
    /// alarm transitions to Shelved (hidden from the active urgency
    /// ranking but still tracked); `tick()` auto-unshelves it once the
    /// deadline passes, restoring UnackActive or RtnUnack based on
    /// whether the underlying condition is still active. The producer
    /// can keep raising/clearing meanwhile -- the condition flag is
    /// updated silently so the restored state is correct.
    void shelve(std::string_view key, std::chrono::seconds duration) {
        bool activeChanged = false;
        const std::string keyCopy(key);
        {
            const std::scoped_lock lock(mutex_);
            auto it = findByKey(key);
            if (it == alarms_.end()) return;
            it->vm.state     = AlarmState::Shelved;
            it->shelvedUntil = now_() + duration;
            activeChanged = true;
        }
        if (activeChanged) {
            signalAlertsChanged_.emit();
            emitAudit("SHELVE", keyCopy);
        }
    }

    /// Operator releases a shelved alarm before its deadline. Restores
    /// UnackActive (condition still active) or RtnUnack (condition has
    /// cleared while shelved). No-op when the alarm isn't shelved.
    void unshelve(std::string_view key) {
        bool activeChanged = false;
        const std::string keyCopy(key);
        {
            const std::scoped_lock lock(mutex_);
            auto it = findByKey(key);
            if (it == alarms_.end() ||
                it->vm.state != AlarmState::Shelved) return;
            it->vm.state = it->conditionActive
                               ? AlarmState::UnackActive
                               : AlarmState::RtnUnack;
            it->shelvedUntil = {};
            activeChanged = true;
        }
        if (activeChanged) {
            signalAlertsChanged_.emit();
            emitAudit("UNSHELVE", keyCopy);
        }
    }

    /// Drives shelf auto-expiry: any shelved alarm whose deadline has
    /// passed transitions back to UnackActive / RtnUnack. The GUI calls
    /// this on every model tick so no Glib timer is needed; tests inject
    /// a controlled clock and call tick() directly.
    void tick() {
        bool activeChanged = false;
        std::vector<std::string> expiredKeys;
        {
            const std::scoped_lock lock(mutex_);
            const TimePoint now = now_();
            for (auto& alarm : alarms_) {
                if (alarm.vm.state == AlarmState::Shelved &&
                    now >= alarm.shelvedUntil) {
                    alarm.vm.state = alarm.conditionActive
                                         ? AlarmState::UnackActive
                                         : AlarmState::RtnUnack;
                    alarm.shelvedUntil = {};
                    activeChanged = true;
                    expiredKeys.push_back(alarm.vm.key);
                }
            }
        }
        if (activeChanged) signalAlertsChanged_.emit();
        for (const auto& k : expiredKeys) emitAudit("EXPIRE", k);
    }

    /// Operator force-resolves every alarm regardless of state (the
    /// "Clear all" button). Each is appended to history.
    void clearAll() {
        bool historyChanged = false;
        std::vector<std::string> resolvedKeys;
        {
            const std::scoped_lock lock(mutex_);
            if (alarms_.empty()) return;
            for (const auto& alarm : alarms_) {
                pushHistoryUnlocked(alarm.vm);
                resolvedKeys.push_back(alarm.vm.key);
                historyChanged = true;
            }
            alarms_.clear();
        }
        signalAlertsChanged_.emit();
        if (historyChanged) signalHistoryChanged_.emit();
        for (const auto& k : resolvedKeys) emitAudit("RESOLVE", k);
    }

    /// Thread-safe snapshot of the active alarms (UnackActive / AckActive
    /// / RtnUnack), projected to view models the panel can render
    /// directly. Ordered by ISA-18.2 priority ascending (most urgent
    /// first); ties keep insertion order so a steady stream of equal-
    /// priority alarms shows the operator the newest at the bottom.
    ///
    /// Phase 4b (REQ-ALARM-005): Shelved alarms are EXCLUDED from this
    /// snapshot -- they belong to `shelvedSnapshot()` instead, so the
    /// panel can render the two inventories as distinct subsections.
    /// Pre-Phase-4b the snapshot included Shelved with a SHELVED badge;
    /// the test helper `stateOf` (in AlertCenterTest) was updated to
    /// look in both snapshots so the lookup contract held.
    [[nodiscard]] std::vector<AlertViewModel> snapshot() const {
        const std::scoped_lock lock(mutex_);
        std::vector<AlertViewModel> out;
        out.reserve(alarms_.size());
        for (const auto& alarm : alarms_) {
            if (alarm.vm.state == AlarmState::Shelved) continue;
            out.push_back(alarm.vm);
        }
        std::stable_sort(out.begin(), out.end(),
            [](const AlertViewModel& a, const AlertViewModel& b) {
                return a.priority < b.priority;
            });
        return out;
    }

    /// Shelved-alarm row delivered to the panel. Pairs the alarm's
    /// view model with the wall-clock deadline + the seconds-remaining
    /// delta from "now" at the moment `shelvedSnapshot()` was called.
    ///
    /// We expose the absolute `deadline` AND the precomputed
    /// `secondsRemaining` because:
    ///   - the panel renders a countdown ("4:37 left") from
    ///     `secondsRemaining`, so it does not need its own clock
    ///   - tests that inject a fake clock can assert on either field
    ///     without having to know AlertCenter's internal NowFn
    ///   - `secondsRemaining` clamps to 0 for entries past their
    ///     deadline (caller can still see them this tick, the next
    ///     `tick()` will sweep them off the shelved list)
    struct ShelvedView {
        AlertViewModel        vm;
        TimePoint             deadline;
        std::chrono::seconds  secondsRemaining;
    };

    /// Thread-safe snapshot of currently-shelved alarms, sorted by
    /// deadline ascending (most-imminent expiry first). Pairs with
    /// `snapshot()` -- the active panel renders one list, the shelved
    /// inventory panel renders the other. See REQ-ALARM-005.
    ///
    /// Phase 4a: this is an additive API; `snapshot()` still includes
    /// Shelved entries so the existing AlertsPanel UI keeps working
    /// unchanged. Phase 4b will switch the UI to render the two lists
    /// from the two snapshots and stop including Shelved in
    /// `snapshot()`.
    [[nodiscard]] std::vector<ShelvedView> shelvedSnapshot() const {
        const std::chrono::system_clock::time_point now = now_();
        const std::scoped_lock lock(mutex_);
        std::vector<ShelvedView> out;
        out.reserve(alarms_.size());
        for (const auto& alarm : alarms_) {
            if (alarm.vm.state != AlarmState::Shelved) continue;
            // `secondsRemaining` clamps at 0 so the panel can render
            // "EXPIRED" without juggling a negative duration. The next
            // tick() will remove these entries; until then the operator
            // still sees them in the inventory.
            const auto delta = std::chrono::duration_cast<std::chrono::seconds>(
                alarm.shelvedUntil - now);
            const auto clamped = delta.count() < 0
                ? std::chrono::seconds{0}
                : delta;
            out.push_back(ShelvedView{
                alarm.vm,
                alarm.shelvedUntil,
                clamped,
            });
        }
        std::sort(out.begin(), out.end(),
            [](const ShelvedView& a, const ShelvedView& b) {
                return a.deadline < b.deadline;
            });
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
    /// `shelvedUntil` is the wall-clock deadline at which the alarm auto-
    /// unshelves; only meaningful while state == Shelved.
    struct Alarm {
        AlertViewModel vm;
        bool           conditionActive{true};
        TimePoint      shelvedUntil{};
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
    NowFn                           now_;
    AuditFn                         auditFn_;

    /// Invoke the audit callback if set. Centralised so every mutator
    /// uses the same one-liner.
    void emitAudit(std::string_view action, std::string_view key) const {
        if (auditFn_) auditFn_(action, key);
    }
};

}  // namespace app::presenter
