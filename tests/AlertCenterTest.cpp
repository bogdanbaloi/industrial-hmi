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

#include <string>

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

// ============================================================================
// raise()
// ============================================================================

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
    // formatNow() returns HH:MM:SS — always contains two colons.
    EXPECT_FALSE(snap[0].timestamp.empty());
    EXPECT_NE(snap[0].timestamp.find(':'), std::string::npos);
}

TEST(AlertCenter, RaisePreservesExplicitTimestamp) {
    AlertCenter c;
    c.raise(makeAlert("k1", AlertSeverity::Info, "t", "m", "12:34:56"));
    EXPECT_EQ(c.snapshot().at(0).timestamp, "12:34:56");
}

// ============================================================================
// clear() / clearAll()
// ============================================================================

TEST(AlertCenter, ClearRemovesByKey) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.raise(makeAlert("k2"));
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

// ============================================================================
// snapshot()
// ============================================================================

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

// ============================================================================
// signalAlertsChanged()
// ============================================================================

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
    EXPECT_EQ(s.count, 0);  // nothing to remove → silent

    c.clear("k1");
    EXPECT_EQ(s.count, 1);
}

TEST(AlertCenter, ClearAllEmitsSignalOnlyWhenListWasNonEmpty) {
    AlertCenter c;
    SignalCounter s{c};

    c.clearAll();
    EXPECT_EQ(s.count, 0);  // already empty → silent

    c.raise(makeAlert("k1"));
    c.clearAll();
    EXPECT_EQ(s.count, 2);  // one from raise, one from clearAll
}

// ============================================================================
// history()
// ============================================================================

// Separate counter for the history signal — parallels SignalCounter but
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

TEST(AlertCenter, ClearPushesAlertToHistory) {
    AlertCenter c;
    c.raise(makeAlert("k1", AlertSeverity::Warning, "tee", "em"));
    c.clear("k1");

    const auto hist = c.history();
    ASSERT_EQ(hist.size(), 1u);
    EXPECT_EQ(hist[0].alert.key, "k1");
    EXPECT_EQ(hist[0].alert.title, "tee");
    EXPECT_FALSE(hist[0].resolvedAt.empty());  // HH:MM:SS stamp
}

TEST(AlertCenter, ClearMissingKeyDoesNotTouchHistory) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.clear("nonexistent");
    EXPECT_TRUE(c.history().empty());
}

TEST(AlertCenter, HistoryIsNewestFirst) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    c.raise(makeAlert("k2"));
    c.raise(makeAlert("k3"));
    c.clear("k1");
    c.clear("k2");
    c.clear("k3");

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

TEST(AlertCenter, ClearEmitsHistorySignal) {
    AlertCenter c;
    c.raise(makeAlert("k1"));
    HistorySignalCounter h{c};
    c.clear("k1");
    EXPECT_EQ(h.count, 1);
}

TEST(AlertCenter, ClearAllEmitsHistorySignalOncePerCall) {
    AlertCenter c;
    c.raise(makeAlert("a"));
    c.raise(makeAlert("b"));
    HistorySignalCounter h{c};
    c.clearAll();
    // Only one notification per batch clear — panel refreshes once.
    EXPECT_EQ(h.count, 1);
}

TEST(AlertCenter, ClearHistoryEmitsHistorySignalOnce) {
    AlertCenter c;
    c.raise(makeAlert("a"));
    c.clear("a");
    HistorySignalCounter h{c};
    c.clearHistory();
    EXPECT_EQ(h.count, 1);
}
