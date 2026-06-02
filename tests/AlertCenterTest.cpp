// [utest->req~quality-002~1]
// [utest->req~alarm-001~1]
// [utest->req~alarm-002~1]
// [utest->req~alarm-003~1]
// [utest->req~alarm-004~1]
// [utest->req~alarm-005~1]
// Covers REQ-QUALITY-002 (alert center),
//        REQ-ALARM-001 (ISA-18.2 alarm lifecycle: UnackActive /
//        AckActive / RtnUnack + acknowledge),
//        REQ-ALARM-002 (Phase 2: shelve / unshelve / auto-expiry on tick),
//        REQ-ALARM-003 (Phase 2: priority distinct from severity),
//        REQ-ALARM-004 (Phase 3: audit journal of lifecycle transitions),
//        REQ-ALARM-005 (Phase 4a: operator-visible Shelved inventory via
//        shelvedSnapshot()).
//
// Tests for app::presenter::AlertCenter
//
// AlertCenter is a tiny header-only service: raise/clear/snapshot + a
// sigc signal emitted whenever the list mutates. These tests pin down
// every visible behavior:
//   - raise inserts, or overwrites an existing entry by `key`
//   - raise stamps an empty timestamp with the current wall-clock time
//   - raise preserves a caller-supplied timestamp
//   - clear removes by key, no-op + no signal if the key isn't present
//   - clearAll wipes, no-op + no signal if already empty
//   - snapshot returns a copy (mutations don't affect prior snapshots)
//   - signalAlertsChanged fires exactly once per mutation

#include "src/presenter/AlertCenter.h"
#include "src/presenter/modelview/AlertViewModel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>

using app::presenter::AlarmState;
using app::presenter::AlertCenter;
using app::presenter::AlertSeverity;
using app::presenter::AlertViewModel;

namespace {

AlertViewModel makeAlert(std::string key,
                         AlertSeverity sev = AlertSeverity::Warning,
                         std::string title = "t",
                         std::string message = "m",
                         std::string timestamp = "") {
    AlertViewModel a;
    a.key       = std::move(key);
    a.severity  = sev;
    a.title     = std::move(title);
    a.message   = std::move(message);
    a.timestamp = std::move(timestamp);
    return a;
}

// Tiny listener that counts signal emissions so tests can assert the
// correct number of notifications per API call.
struct SignalCounter {
    int count = 0;
    sigc::connection conn;

    explicit SignalCounter(AlertCenter& c) {
        conn = c.signalAlertsChanged().connect([this]() { ++count; });
    }
    ~SignalCounter() { conn.disconnect(); }
};

}  // namespace

// raise()

TEST(AlertCenter, RaiseInsertsNewAlert) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    const auto snap = c.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].key, "k1");
}

TEST(AlertCenter, RaiseWithSameKeyReplacesExisting) {
    AlertCenter c;
    c.raise(makeAlert("k1", AlertSeverity::Warning, "first"));
    c.raise(makeAlert("k1", AlertSeverity::Critical, "second"));
    const auto snap = c.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].title, "second");
    EXPECT_EQ(snap[0].severity, AlertSeverity::Critical);
}

TEST(AlertCenter, RaiseDifferentKeysCoexist) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.raise(makeAlert("k2"));
    c.raise(makeAlert("k3"));
    EXPECT_EQ(c.snapshot().size(), 3u);
}

TEST(AlertCenter, RaiseEmptyTimestampGetsStamped) {
    AlertCenter c;
    c.raise(makeAlert("k1", AlertSeverity::Info, "t", "m", /*timestamp*/ ""));
    const auto snap = c.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    // formatNow() returns HH:MM:SS -- always contains two colons.
    EXPECT_FALSE(snap[0].timestamp.empty());
    EXPECT_NE(snap[0].timestamp.find(':'), std::string::npos);
}

TEST(AlertCenter, RaisePreservesExplicitTimestamp) {
    AlertCenter c;
    c.raise(makeAlert("k1", AlertSeverity::Info, "t", "m", "12:34:56"));
    EXPECT_EQ(c.snapshot().at(0).timestamp, "12:34:56");
}

// clear() / clearAll()

// ISA-18.2: a condition returning to normal while UNACKNOWLEDGED does not
// remove the alarm -- it transitions to RtnUnack and stays visible.
TEST(AlertCenter, ClearOnUnackBecomesRtnUnackAndStaysVisible) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.raise(makeAlert("k2"));
    c.clear("k1");
    const auto snap = c.snapshot();
    ASSERT_EQ(snap.size(), 2u) << "RtnUnack alarm must stay in the list";
    const auto k1 = std::find_if(snap.begin(), snap.end(),
        [](const AlertViewModel& a) { return a.key == "k1"; });
    ASSERT_NE(k1, snap.end());
    EXPECT_EQ(k1->state, app::presenter::AlarmState::RtnUnack);
}

// An ACKNOWLEDGED alarm whose condition then clears is fully resolved.
TEST(AlertCenter, ClearOnAckedAlarmRemovesIt) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.raise(makeAlert("k2"));
    c.acknowledge("k1");
    c.clear("k1");
    const auto snap = c.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].key, "k2");
}

TEST(AlertCenter, ClearMissingKeyIsNoOp) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.clear("nonexistent");
    EXPECT_EQ(c.snapshot().size(), 1u);
}

TEST(AlertCenter, ClearAllWipesList) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.raise(makeAlert("k2"));
    c.clearAll();
    EXPECT_TRUE(c.snapshot().empty());
}

TEST(AlertCenter, ClearAllOnEmptyIsNoOp) {
    AlertCenter c;
    c.clearAll();  // must not crash / throw
    EXPECT_TRUE(c.snapshot().empty());
}

// snapshot()

TEST(AlertCenter, SnapshotReturnsIndependentCopy) {
    AlertCenter c;
    c.raise(makeAlert("k1", AlertSeverity::Info, "first"));
    const auto snap1 = c.snapshot();

    c.raise(makeAlert("k1", AlertSeverity::Critical, "updated"));
    const auto snap2 = c.snapshot();

    // snap1 kept its original contents, snap2 reflects the update.
    EXPECT_EQ(snap1.at(0).title, "first");
    EXPECT_EQ(snap2.at(0).title, "updated");
}

// signalAlertsChanged()

TEST(AlertCenter, RaiseEmitsSignal) {
    AlertCenter c;
    SignalCounter s{c};
    c.raise(makeAlert("k1"));
    EXPECT_EQ(s.count, 1);
}

TEST(AlertCenter, DuplicateRaiseStillEmitsSignal) {
    // Even when the alert is "the same", we still want the UI to
    // refresh because the timestamp (and potentially other fields)
    // were replaced.
    AlertCenter c;
    SignalCounter s{c};
    c.raise(makeAlert("k1", AlertSeverity::Warning, "same"));
    c.raise(makeAlert("k1", AlertSeverity::Warning, "same"));
    EXPECT_EQ(s.count, 2);
}

TEST(AlertCenter, ClearEmitsSignalOnlyWhenSomethingRemoved) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    SignalCounter s{c};

    c.clear("missing");
    EXPECT_EQ(s.count, 0);  // nothing to remove -> silent

    c.clear("k1");
    EXPECT_EQ(s.count, 1);
}

TEST(AlertCenter, ClearAllEmitsSignalOnlyWhenListWasNonEmpty) {
    AlertCenter c;
    SignalCounter s{c};

    c.clearAll();
    EXPECT_EQ(s.count, 0);  // already empty -> silent

    c.raise(makeAlert("k1"));
    c.clearAll();
    EXPECT_EQ(s.count, 2);  // one from raise, one from clearAll
}

// history()

// Separate counter for the history signal -- parallels SignalCounter but
// subscribes to signalHistoryChanged.
struct HistorySignalCounter {
    int count = 0;
    sigc::connection conn;
    explicit HistorySignalCounter(AlertCenter& c) {
        conn = c.signalHistoryChanged().connect([this]() { ++count; });
    }
    ~HistorySignalCounter() { conn.disconnect(); }
};

TEST(AlertCenter, HistoryIsEmptyByDefault) {
    AlertCenter c;
    EXPECT_TRUE(c.history().empty());
}

TEST(AlertCenter, RaiseDoesNotTouchHistory) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.raise(makeAlert("k1", AlertSeverity::Critical));  // replacement
    EXPECT_TRUE(c.history().empty());
}

TEST(AlertCenter, ResolvedAlarmGoesToHistory) {
    AlertCenter c;
    c.raise(makeAlert("k1", AlertSeverity::Warning, "tee", "em"));
    c.clear("k1");        // -> RtnUnack (still active, not yet in history)
    EXPECT_TRUE(c.history().empty());
    c.acknowledge("k1");  // RtnUnack + ack -> resolved -> history

    const auto hist = c.history();
    ASSERT_EQ(hist.size(), 1u);
    EXPECT_EQ(hist[0].alert.key, "k1");
    EXPECT_EQ(hist[0].alert.title, "tee");
    EXPECT_FALSE(hist[0].resolvedAt.empty());  // HH:MM:SS stamp
    EXPECT_TRUE(c.snapshot().empty());
}

TEST(AlertCenter, ClearMissingKeyDoesNotTouchHistory) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.clear("nonexistent");
    EXPECT_TRUE(c.history().empty());
}

TEST(AlertCenter, HistoryIsNewestFirst) {
    AlertCenter c;
    // Acknowledge-then-clear fully resolves each alarm into history.
    for (const auto* k : {"k1", "k2", "k3"}) {
        c.raise(makeAlert(k));
        c.acknowledge(k);
        c.clear(k);
    }

    const auto hist = c.history();
    ASSERT_EQ(hist.size(), 3u);
    EXPECT_EQ(hist[0].alert.key, "k3");  // most recent first
    EXPECT_EQ(hist[1].alert.key, "k2");
    EXPECT_EQ(hist[2].alert.key, "k1");
}

TEST(AlertCenter, ClearAllPushesEveryAlertToHistory) {
    AlertCenter c;
    c.raise(makeAlert("a"));
    c.raise(makeAlert("b"));
    c.raise(makeAlert("c"));
    c.clearAll();

    EXPECT_EQ(c.history().size(), 3u);
}

TEST(AlertCenter, HistoryIsBoundedByCapacity) {
    AlertCenter c;
    const std::size_t over = AlertCenter::kHistoryCapacity + 10;
    for (std::size_t i = 0; i < over; ++i) {
        const auto key = "k" + std::to_string(i);
        c.raise(makeAlert(key));
        c.acknowledge(key);  // ack-then-clear resolves into history
        c.clear(key);
    }
    EXPECT_EQ(c.history().size(), AlertCenter::kHistoryCapacity);

    // Oldest entries (k0..k9) should have been dropped; k{over-1} at front.
    const auto hist = c.history();
    EXPECT_EQ(hist.front().alert.key, "k" + std::to_string(over - 1));
}

TEST(AlertCenter, ClearHistoryWipesList) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.acknowledge("k1");
    c.clear("k1");
    ASSERT_EQ(c.history().size(), 1u);

    c.clearHistory();
    EXPECT_TRUE(c.history().empty());
}

TEST(AlertCenter, ClearHistoryOnEmptyIsNoOpSignal) {
    AlertCenter c;
    HistorySignalCounter h{c};
    c.clearHistory();
    EXPECT_EQ(h.count, 0);
}

TEST(AlertCenter, ResolvingAlarmEmitsHistorySignal) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.acknowledge("k1");
    HistorySignalCounter h{c};
    c.clear("k1");  // AckActive + clear -> resolved -> history
    EXPECT_EQ(h.count, 1);
}

TEST(AlertCenter, ClearAllEmitsHistorySignalOncePerCall) {
    AlertCenter c;
    c.raise(makeAlert("a"));
    c.raise(makeAlert("b"));
    HistorySignalCounter h{c};
    c.clearAll();
    // Only one notification per batch clear -- panel refreshes once.
    EXPECT_EQ(h.count, 1);
}

TEST(AlertCenter, ClearHistoryEmitsHistorySignalOnce) {
    AlertCenter c;
    c.raise(makeAlert("a"));
    c.acknowledge("a");
    c.clear("a");
    HistorySignalCounter h{c};
    c.clearHistory();
    EXPECT_EQ(h.count, 1);
}

// ISA-18.2 alarm lifecycle (REQ-ALARM-001)

namespace {
// Convenience: state of the single alarm with `key`, regardless of which
// snapshot it currently lives in. Phase 4b filters Shelved entries out
// of snapshot() and surfaces them via shelvedSnapshot() instead, so a
// state lookup that only checked snapshot() would lose visibility on
// Shelved alarms after they are shelved. The helper checks both lists.
AlarmState stateOf(const AlertCenter& c, const std::string& key) {
    const auto snap = c.snapshot();
    const auto it = std::find_if(snap.begin(), snap.end(),
        [&](const AlertViewModel& a) { return a.key == key; });
    if (it != snap.end()) return it->state;

    const auto shelved = c.shelvedSnapshot();
    const auto sit = std::find_if(shelved.begin(), shelved.end(),
        [&](const AlertCenter::ShelvedView& v) { return v.vm.key == key; });
    if (sit != shelved.end()) return sit->vm.state;

    return AlarmState::RtnUnack;  // sentinel: not found
}
}  // namespace

TEST(AlertCenter, RaiseStartsUnacknowledged) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    EXPECT_EQ(stateOf(c, "k1"), AlarmState::UnackActive);
}

TEST(AlertCenter, AcknowledgeActiveAlarmBecomesAckedAndStaysVisible) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.acknowledge("k1");
    ASSERT_EQ(c.snapshot().size(), 1u) << "an acked-but-active alarm stays";
    EXPECT_EQ(stateOf(c, "k1"), AlarmState::AckActive);
}

TEST(AlertCenter, AcknowledgeRtnUnackResolvesToHistory) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.clear("k1");                 // -> RtnUnack (still visible)
    ASSERT_EQ(stateOf(c, "k1"), AlarmState::RtnUnack);
    c.acknowledge("k1");           // RtnUnack + ack -> resolved
    EXPECT_TRUE(c.snapshot().empty());
    EXPECT_EQ(c.history().size(), 1u);
}

TEST(AlertCenter, ConditionReactivatingFromRtnUnackReAlarms) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.clear("k1");                 // -> RtnUnack
    c.raise(makeAlert("k1"));      // condition active again -> re-alarm
    EXPECT_EQ(stateOf(c, "k1"), AlarmState::UnackActive);
    EXPECT_TRUE(c.history().empty()) << "re-alarm is not a resolution";
}

TEST(AlertCenter, RaiseDoesNotDowngradeAnAcknowledgedAlarm) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.acknowledge("k1");
    c.raise(makeAlert("k1", AlertSeverity::Critical, "still firing"));
    // A still-firing acknowledged alarm must not bounce back to UNACK.
    EXPECT_EQ(stateOf(c, "k1"), AlarmState::AckActive);
}

TEST(AlertCenter, AcknowledgeMissingKeyIsNoOp) {
    AlertCenter c;
    c.acknowledge("nope");  // must not crash
    EXPECT_TRUE(c.snapshot().empty());
}

// ISA-18.2 Phase 2 -- shelve / unshelve / auto-expiry (REQ-ALARM-002)

namespace {
// Minimal controllable clock for shelve auto-expiry tests. AlertCenter
// only reads the current time when computing shelve deadlines + on each
// tick(); a value-returning closure backed by this struct gives the
// test deterministic control without any sleeps.
struct FakeClock {
    AlertCenter::TimePoint now{};
    AlertCenter::TimePoint operator()() const { return now; }
};

// Convenience: AlertCenter wired to a fake clock owned by the test.
AlertCenter centerWithFakeClock(std::shared_ptr<FakeClock> clock) {
    return AlertCenter{[clock] { return clock->now; }};
}
}  // namespace

TEST(AlertCenter, ShelveHidesAlarmFromActiveLifecycleUntilExpiry) {
    auto clock = std::make_shared<FakeClock>();
    AlertCenter c = centerWithFakeClock(clock);
    c.raise(makeAlert("k1"));
    ASSERT_EQ(stateOf(c, "k1"), AlarmState::UnackActive);

    c.shelve("k1", std::chrono::seconds{60});
    EXPECT_EQ(stateOf(c, "k1"), AlarmState::Shelved);

    // Tick before deadline -- still shelved.
    clock->now += std::chrono::seconds{30};
    c.tick();
    EXPECT_EQ(stateOf(c, "k1"), AlarmState::Shelved);

    // Tick past deadline -- auto-unshelves to UnackActive (condition
    // still holds because no clear() was received in the meantime).
    clock->now += std::chrono::seconds{31};
    c.tick();
    EXPECT_EQ(stateOf(c, "k1"), AlarmState::UnackActive);
}

TEST(AlertCenter, ShelveExpiryRestoresRtnUnackIfConditionClearedWhileShelved) {
    auto clock = std::make_shared<FakeClock>();
    AlertCenter c = centerWithFakeClock(clock);
    c.raise(makeAlert("k1"));
    c.shelve("k1", std::chrono::seconds{60});

    // Condition clears silently while the alarm is shelved.
    c.clear("k1");
    EXPECT_EQ(stateOf(c, "k1"), AlarmState::Shelved);

    // Auto-unshelve sees the cleared condition and returns to RtnUnack.
    clock->now += std::chrono::seconds{61};
    c.tick();
    EXPECT_EQ(stateOf(c, "k1"), AlarmState::RtnUnack);
}

TEST(AlertCenter, UnshelveBeforeDeadlineRestoresPriorActiveState) {
    auto clock = std::make_shared<FakeClock>();
    AlertCenter c = centerWithFakeClock(clock);
    c.raise(makeAlert("k1"));
    c.shelve("k1", std::chrono::seconds{60});

    c.unshelve("k1");
    EXPECT_EQ(stateOf(c, "k1"), AlarmState::UnackActive);

    clock->now += std::chrono::seconds{120};
    c.tick();  // already unshelved -- tick is a no-op
    EXPECT_EQ(stateOf(c, "k1"), AlarmState::UnackActive);
}

TEST(AlertCenter, UnshelveOnNonShelvedAlarmIsNoOp) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.unshelve("k1");  // not shelved -- must not crash, no state change
    EXPECT_EQ(stateOf(c, "k1"), AlarmState::UnackActive);
}

// ISA-18.2 Phase 4a -- operator-visible Shelved inventory (REQ-ALARM-005)
//
// `shelvedSnapshot()` is an additive read API alongside the existing
// `snapshot()` (Phase 1) so the panel can render two distinct lists:
// active alarms ordered by priority, and shelved alarms ordered by
// most-imminent expiry. Phase 4b will rewire the GTK AlertsPanel to
// consume both; Phase 4a only covers the model+presenter contract.

TEST(AlertCenter, ShelvedSnapshotEmptyWhenNoShelvedAlarms) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.raise(makeAlert("k2"));
    EXPECT_TRUE(c.shelvedSnapshot().empty());
}

TEST(AlertCenter, ShelvedSnapshotIncludesShelvedAlarmsOnly) {
    auto clock = std::make_shared<FakeClock>();
    AlertCenter c = centerWithFakeClock(clock);
    c.raise(makeAlert("k1"));
    c.raise(makeAlert("k2"));
    c.raise(makeAlert("k3"));
    c.shelve("k2", std::chrono::seconds{60});

    const auto shelved = c.shelvedSnapshot();
    ASSERT_EQ(shelved.size(), 1u);
    EXPECT_EQ(shelved[0].vm.key, "k2");
    EXPECT_EQ(shelved[0].vm.state, AlarmState::Shelved);
}

TEST(AlertCenter, ShelvedSnapshotOrderedByDeadlineAscending) {
    auto clock = std::make_shared<FakeClock>();
    AlertCenter c = centerWithFakeClock(clock);
    c.raise(makeAlert("k1"));
    c.raise(makeAlert("k2"));
    c.raise(makeAlert("k3"));

    // Shelve at increasing durations -- k1 expires soonest, k3 last.
    c.shelve("k1", std::chrono::seconds{30});
    c.shelve("k2", std::chrono::seconds{120});
    c.shelve("k3", std::chrono::seconds{60});

    const auto shelved = c.shelvedSnapshot();
    ASSERT_EQ(shelved.size(), 3u);
    // Most-imminent expiry first -- the inventory panel's natural
    // operator-attention order.
    EXPECT_EQ(shelved[0].vm.key, "k1");
    EXPECT_EQ(shelved[1].vm.key, "k3");
    EXPECT_EQ(shelved[2].vm.key, "k2");
}

TEST(AlertCenter, ShelvedSnapshotSecondsRemainingFollowsClock) {
    auto clock = std::make_shared<FakeClock>();
    AlertCenter c = centerWithFakeClock(clock);
    c.raise(makeAlert("k1"));
    c.shelve("k1", std::chrono::seconds{60});

    auto shelved = c.shelvedSnapshot();
    ASSERT_EQ(shelved.size(), 1u);
    EXPECT_EQ(shelved[0].secondsRemaining, std::chrono::seconds{60});

    // Advance clock 25s -- countdown should reflect remaining 35s.
    clock->now += std::chrono::seconds{25};
    shelved = c.shelvedSnapshot();
    ASSERT_EQ(shelved.size(), 1u);
    EXPECT_EQ(shelved[0].secondsRemaining, std::chrono::seconds{35});
}

TEST(AlertCenter, ShelvedSnapshotSecondsRemainingClampsToZeroPastDeadline) {
    auto clock = std::make_shared<FakeClock>();
    AlertCenter c = centerWithFakeClock(clock);
    c.raise(makeAlert("k1"));
    c.shelve("k1", std::chrono::seconds{60});

    // Past deadline but BEFORE tick() runs: entry is still in the
    // snapshot, secondsRemaining clamps to 0 so the panel renders
    // "EXPIRED" without juggling negative durations.
    clock->now += std::chrono::seconds{90};
    const auto shelved = c.shelvedSnapshot();
    ASSERT_EQ(shelved.size(), 1u);
    EXPECT_EQ(shelved[0].secondsRemaining, std::chrono::seconds{0});
}

TEST(AlertCenter, ShelvedSnapshotEmptyAfterTickSweepsExpired) {
    auto clock = std::make_shared<FakeClock>();
    AlertCenter c = centerWithFakeClock(clock);
    c.raise(makeAlert("k1"));
    c.shelve("k1", std::chrono::seconds{60});

    clock->now += std::chrono::seconds{90};
    c.tick();  // auto-unshelves expired entries
    EXPECT_TRUE(c.shelvedSnapshot().empty());
}

TEST(AlertCenter, ShelvedSnapshotDeadlineMatchesShelveCall) {
    auto clock = std::make_shared<FakeClock>();
    AlertCenter c = centerWithFakeClock(clock);
    c.raise(makeAlert("k1"));

    const auto t0 = clock->now;
    c.shelve("k1", std::chrono::seconds{300});
    const auto shelved = c.shelvedSnapshot();
    ASSERT_EQ(shelved.size(), 1u);
    EXPECT_EQ(shelved[0].deadline, t0 + std::chrono::seconds{300});
}

// ISA-18.2 Phase 2 -- priority sorting (REQ-ALARM-003)

TEST(AlertCenter, SnapshotIsOrderedByPriorityAscending) {
    AlertCenter c;
    AlertViewModel low      = makeAlert("low");      low.priority = 4;
    AlertViewModel medium   = makeAlert("medium");   medium.priority = 3;
    AlertViewModel high     = makeAlert("high");     high.priority = 2;
    AlertViewModel emergency = makeAlert("emerg");   emergency.priority = 1;

    // Insert in shuffled order.
    c.raise(medium);
    c.raise(low);
    c.raise(emergency);
    c.raise(high);

    const auto snap = c.snapshot();
    ASSERT_EQ(snap.size(), 4u);
    // Most urgent first: 1, 2, 3, 4.
    EXPECT_EQ(snap[0].key, "emerg");
    EXPECT_EQ(snap[1].key, "high");
    EXPECT_EQ(snap[2].key, "medium");
    EXPECT_EQ(snap[3].key, "low");
}

TEST(AlertCenter, EqualPrioritiesKeepInsertionOrder) {
    AlertCenter c;
    AlertViewModel first  = makeAlert("first");  first.priority  = 3;
    AlertViewModel second = makeAlert("second"); second.priority = 3;
    c.raise(first);
    c.raise(second);
    const auto snap = c.snapshot();
    ASSERT_EQ(snap.size(), 2u);
    EXPECT_EQ(snap[0].key, "first");
    EXPECT_EQ(snap[1].key, "second");
}

// ISA-18.2 Phase 3 -- audit journal (REQ-ALARM-004)

namespace {
struct AuditRecord {
    std::string action;
    std::string key;
};

// Convenience: build a recording audit callback and a vector it appends
// into. The shared_ptr lets the lambda outlive its local capture.
auto makeRecordingAudit(std::shared_ptr<std::vector<AuditRecord>> log) {
    return [log](std::string_view a, std::string_view k) {
        log->push_back({std::string(a), std::string(k)});
    };
}
}  // namespace

TEST(AlertCenter, AuditEmitsRaiseAckResolveOnHappyPath) {
    auto log = std::make_shared<std::vector<AuditRecord>>();
    AlertCenter c;
    c.setAuditCallback(makeRecordingAudit(log));

    c.raise(makeAlert("k1"));   // -> RAISE
    c.acknowledge("k1");        // -> ACK
    c.clear("k1");              // -> RESOLVE (AckActive + clear)

    ASSERT_EQ(log->size(), 3u);
    EXPECT_EQ((*log)[0].action, "RAISE");
    EXPECT_EQ((*log)[0].key,    "k1");
    EXPECT_EQ((*log)[1].action, "ACK");
    EXPECT_EQ((*log)[2].action, "RESOLVE");
}

TEST(AlertCenter, AuditEmitsRtnThenResolveWhenAckAfterReturn) {
    auto log = std::make_shared<std::vector<AuditRecord>>();
    AlertCenter c;
    c.setAuditCallback(makeRecordingAudit(log));

    c.raise(makeAlert("k1"));   // RAISE
    c.clear("k1");              // RTN (UnackActive + clear -> RtnUnack)
    c.acknowledge("k1");        // RESOLVE (RtnUnack + ack)

    ASSERT_EQ(log->size(), 3u);
    EXPECT_EQ((*log)[1].action, "RTN");
    EXPECT_EQ((*log)[2].action, "RESOLVE");
}

TEST(AlertCenter, AuditEmitsRealarmOnReactivation) {
    auto log = std::make_shared<std::vector<AuditRecord>>();
    AlertCenter c;
    c.setAuditCallback(makeRecordingAudit(log));

    c.raise(makeAlert("k1"));   // RAISE
    c.clear("k1");              // RTN
    c.raise(makeAlert("k1"));   // REALARM (RtnUnack + raise)

    ASSERT_EQ(log->size(), 3u);
    EXPECT_EQ((*log)[2].action, "REALARM");
}

TEST(AlertCenter, AuditEmitsShelveUnshelve) {
    auto log = std::make_shared<std::vector<AuditRecord>>();
    AlertCenter c;
    c.setAuditCallback(makeRecordingAudit(log));

    c.raise(makeAlert("k1"));
    c.shelve("k1", std::chrono::seconds{60});
    c.unshelve("k1");

    ASSERT_EQ(log->size(), 3u);
    EXPECT_EQ((*log)[1].action, "SHELVE");
    EXPECT_EQ((*log)[2].action, "UNSHELVE");
}

TEST(AlertCenter, AuditEmitsExpireOnAutoUnshelve) {
    auto clock = std::make_shared<FakeClock>();
    auto log = std::make_shared<std::vector<AuditRecord>>();
    AlertCenter c = centerWithFakeClock(clock);
    c.setAuditCallback(makeRecordingAudit(log));

    c.raise(makeAlert("k1"));
    c.shelve("k1", std::chrono::seconds{60});
    clock->now += std::chrono::seconds{61};
    c.tick();

    ASSERT_EQ(log->size(), 3u);
    EXPECT_EQ((*log)[2].action, "EXPIRE");
    EXPECT_EQ((*log)[2].key,    "k1");
}

TEST(AlertCenter, AuditDoesNotFireOnRefreshOfActiveAlarm) {
    auto log = std::make_shared<std::vector<AuditRecord>>();
    AlertCenter c;
    c.setAuditCallback(makeRecordingAudit(log));

    c.raise(makeAlert("k1"));
    c.raise(makeAlert("k1", AlertSeverity::Critical, "louder"));

    // A refresh of an already-active alarm is not a lifecycle transition
    // and must NOT pollute the audit log.
    ASSERT_EQ(log->size(), 1u);
    EXPECT_EQ((*log)[0].action, "RAISE");
}
