// [utest->req~historian-002~1]
// Covers REQ-HISTORIAN-002 (batched writes).
//
// Tests for HistorianBridge.
//
// The SimulatedModel singleton owns its callback list and has no
// `removeObserver` path -- every bridge constructed in a test would
// leave a stale `[this]` capture in the singleton, blowing up the
// next test when the model fires the dead lambda. Two consequences:
//
//   1. We wire ONE bridge in `SetUpTestSuite()`, share it across every
//      TEST_F, and never destroy it during the suite. The same idiom
//      SimulatedModelTest uses (`shared_ptr` capture) wouldn't help
//      here because the bridge itself is the closure-captured object.
//   2. The FakeHistoryWriter is also static so it has the same
//      lifetime; we `reset()` it between tests instead.

#include "src/historian/HistorianBridge.h"

#include "src/historian/HistoryRecord.h"
#include "src/historian/HistoryWriter.h"
#include "src/model/SimulatedModel.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using app::historian::FieldKind;
using app::historian::HistorianBridge;
using app::historian::HistoryRecord;
using app::historian::HistoryWriter;

/// Test double -- stores every row the bridge ships and lets the test
/// count flushes. Mutex because SimulatedModel may invoke callbacks
/// from a thread the test isn't on.
class FakeHistoryWriter : public HistoryWriter {
public:
    std::size_t write(std::span<const HistoryRecord> records) override {
        const std::scoped_lock lock(mutex_);
        ++flushes_;
        all_.insert(all_.end(), records.begin(), records.end());
        return records.size();
    }

    std::vector<HistoryRecord> snapshot() const {
        const std::scoped_lock lock(mutex_);
        return all_;
    }
    std::size_t flushes() const {
        const std::scoped_lock lock(mutex_);
        return flushes_;
    }

    /// Clear accumulated state between tests so per-test assertions
    /// see only what *that test* produced.
    void reset() {
        const std::scoped_lock lock(mutex_);
        all_.clear();
        flushes_ = 0;
    }

private:
    mutable std::mutex          mutex_;
    std::vector<HistoryRecord>  all_;
    std::size_t                 flushes_{0};
};

}  // namespace

/// Fixture that holds the long-lived (process-lifetime) bridge and fake.
/// See file header for why we can't construct/destroy per test.
class HistorianBridgeTest : public ::testing::Test {
protected:
    static FakeHistoryWriter*                       fake_;
    static std::unique_ptr<HistorianBridge>         bridge_;

    static void SetUpTestSuite() {
        // Demo data populates the maps the analog setters guard on
        // (`setQualityPassRate` drops unknown ids silently).
        app::model::SimulatedModel::instance().initializeDemoData();

        fake_   = new FakeHistoryWriter();   // intentionally leaked
        HistorianBridge::Config cfg;
        cfg.maxBatchSize = 1;                // flush every row by default
        cfg.maxBatchAge  = std::chrono::seconds{60};
        bridge_ = std::make_unique<HistorianBridge>(
            *fake_, app::model::SimulatedModel::instance(), cfg);
        bridge_->wire();
    }

    static void TearDownTestSuite() {
        // Bridge MUST be released here so the lambda captures resolve
        // to a still-valid `this` for the duration of every test, but
        // dies before the test process tears the SimulatedModel down
        // (otherwise the model's destructor walking the callback list
        // would hit a dead pointer too).
        bridge_.reset();
        // fake_ is intentionally leaked -- a callback in another test
        // suite might still fire against the model and walk into us.
    }

    void SetUp() override { fake_->reset(); }
};

FakeHistoryWriter* HistorianBridgeTest::fake_ = nullptr;
std::unique_ptr<HistorianBridge> HistorianBridgeTest::bridge_;

TEST_F(HistorianBridgeTest, EquipmentSupplyLevelChangeProducesRow) {
    auto& model = app::model::SimulatedModel::instance();
    model.setEquipmentEnabled(0, true);
    model.setEquipmentSupplyLevel(0, 72);
    bridge_->flush();

    bool foundSupply = false;
    for (const auto& r : fake_->snapshot()) {
        if (r.field == FieldKind::EquipmentSupplyLevel
                && r.entityId == 0
                && r.value > 0.0F) {
            foundSupply = true;
        }
    }
    EXPECT_TRUE(foundSupply)
        << "expected at least one EquipmentSupplyLevel row for entity 0";
}

TEST_F(HistorianBridgeTest, QualityChangeProducesPassRateRow) {
    auto& model = app::model::SimulatedModel::instance();
    model.setQualityPassRate(0, 87.5F);
    bridge_->flush();

    bool found = false;
    for (const auto& r : fake_->snapshot()) {
        if (r.field == FieldKind::QualityPassRate
                && r.entityId == 0
                && r.value > 80.0F && r.value < 95.0F) {
            found = true;
        }
    }
    EXPECT_TRUE(found)
        << "expected QualityPassRate row near 87.5 for checkpoint 0";
}

TEST_F(HistorianBridgeTest, SystemStateTransitionProducesRow) {
    auto& model = app::model::SimulatedModel::instance();
    // Force a transition: stop first (idempotent if already IDLE), then
    // start, which always fires onSystemStateChanged.
    model.stopProduction();
    fake_->reset();
    model.startProduction();
    bridge_->flush();

    bool foundState = false;
    for (const auto& r : fake_->snapshot()) {
        if (r.field == FieldKind::SystemState) {
            foundState = true;
        }
    }
    EXPECT_TRUE(foundState)
        << "expected SystemState row on transition";
    model.stopProduction();
}

TEST_F(HistorianBridgeTest, ExplicitFlushIsIdempotent) {
    // Calling flush() twice in a row must be a no-op the second time --
    // the composition root calls flush() on shutdown after the model
    // has stopped publishing, and the destructor also calls flush().
    bridge_->flush();
    const auto before = fake_->flushes();
    bridge_->flush();
    EXPECT_EQ(fake_->flushes(), before)
        << "flush() with empty buffer should not call the writer";
}

TEST_F(HistorianBridgeTest, TimestampsAreNonZero) {
    auto& model = app::model::SimulatedModel::instance();
    model.setQualityPassRate(0, 80.0F);
    bridge_->flush();

    const auto rows = fake_->snapshot();
    ASSERT_FALSE(rows.empty());
    for (const auto& r : rows) {
        EXPECT_GT(r.timestampMs, 0);
    }
}
