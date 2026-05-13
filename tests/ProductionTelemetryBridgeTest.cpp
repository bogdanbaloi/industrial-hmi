// Tests for app::integration::ProductionTelemetryBridge.
//
// The bridge is the domain-to-publisher seam. Its job is to subscribe
// to ProductionModel signals and call TelemetryPublisher::publish()
// with the right topic + payload. We verify by capturing every call
// in a fake publisher, then driving the model side via the gmock
// callback-storing pattern.
//
// No sockets, no GTK -- pure callback plumbing. ~zero ms per test.

#include "src/integration/ProductionTelemetryBridge.h"

#include "src/integration/TelemetryPublisher.h"
#include "src/model/ProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

using app::integration::ProductionTelemetryBridge;
using app::integration::TelemetryPublisher;
using app::model::EquipmentStatus;
using app::model::ProductionModel;
using app::model::QualityCheckpoint;
using app::model::SystemState;

namespace {

/// In-memory TelemetryPublisher that records every publish() call.
/// Preferred over gmock for this case because the assertions read
/// like a transcript: "the third publish was equipment/2/state with
/// payload 'fault'".
class CapturingPublisher : public TelemetryPublisher {
public:
    struct Entry {
        std::string topic;
        std::string payload;
    };

    void publish(const std::string& topic,
                 const std::string& payload) override {
        entries.push_back({topic, payload});
    }

    [[nodiscard]] bool canPublish() const override { return true; }

    std::vector<Entry> entries;
};

/// MockProductionModel that stores the registered callbacks so the
/// test body can fire them on demand (mirroring the real model's
/// signal dispatch without needing a live SimulatedModel).
class CapturingProductionModel : public ProductionModel {
public:
    void onEquipmentStatusChanged(EquipmentCallback cb) override {
        equipmentCb_ = std::move(cb);
    }
    void onActuatorStatusChanged(ActuatorCallback cb) override {
        actuatorCb_ = std::move(cb);
    }
    void onQualityCheckpointChanged(QualityCheckpointCallback cb) override {
        qualityCb_ = std::move(cb);
    }
    void onWorkUnitChanged(WorkUnitCallback cb) override {
        workUnitCb_ = std::move(cb);
    }
    void onSystemStateChanged(StateCallback cb) override {
        stateCb_ = std::move(cb);
    }

    // Commands -- not exercised by the bridge.
    void startProduction() override {}
    void stopProduction() override {}
    void resetSystem() override {}
    void startCalibration() override {}
    void setEquipmentEnabled(uint32_t, bool) override {}
    void setEquipmentSupplyLevel(uint32_t, int) override {}
    void setQualityPassRate(uint32_t, float) override {}

    // Queries -- not exercised by the bridge.
    [[nodiscard]] SystemState getState() const override {
        return SystemState::IDLE;
    }
    [[nodiscard]] QualityCheckpoint
        getQualityCheckpoint(uint32_t) const override { return {}; }
    [[nodiscard]] app::model::WorkUnit getWorkUnit() const override {
        return {};
    }

    // Test fire helpers
    void fireSystemState(SystemState s) { if (stateCb_) stateCb_(s); }
    void fireEquipment(const EquipmentStatus& es) {
        if (equipmentCb_) equipmentCb_(es);
    }
    void fireQuality(const QualityCheckpoint& qc) {
        if (qualityCb_) qualityCb_(qc);
    }

private:
    EquipmentCallback equipmentCb_;
    ActuatorCallback actuatorCb_;
    QualityCheckpointCallback qualityCb_;
    WorkUnitCallback workUnitCb_;
    StateCallback stateCb_;
};

ProductionTelemetryBridge::Config makeConfig(std::string prefix = "test-prefix") {
    ProductionTelemetryBridge::Config c;
    c.topicPrefix = std::move(prefix);
    return c;
}

}  // namespace

// SystemState

TEST(ProductionTelemetryBridgeTest, PublishesSystemStateOnTransition) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge bridge(pub, model, makeConfig());
    bridge.wire();

    model.fireSystemState(SystemState::RUNNING);

    ASSERT_EQ(pub.entries.size(), 1U);
    EXPECT_EQ(pub.entries[0].topic,   "test-prefix/state");
    EXPECT_EQ(pub.entries[0].payload, "running");
}

TEST(ProductionTelemetryBridgeTest, MapsEverySystemStateToStableString) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge bridge(pub, model, makeConfig());
    bridge.wire();

    model.fireSystemState(SystemState::IDLE);
    model.fireSystemState(SystemState::RUNNING);
    model.fireSystemState(SystemState::ERROR);
    model.fireSystemState(SystemState::CALIBRATION);

    ASSERT_EQ(pub.entries.size(), 4U);
    EXPECT_EQ(pub.entries[0].payload, "idle");
    EXPECT_EQ(pub.entries[1].payload, "running");
    EXPECT_EQ(pub.entries[2].payload, "error");
    EXPECT_EQ(pub.entries[3].payload, "calibration");
}

// Equipment

TEST(ProductionTelemetryBridgeTest, EquipmentStatusPublishesNamedState) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge bridge(pub, model, makeConfig());
    bridge.wire();

    // Every status integer maps to its own named string -- so a SCADA
    // can distinguish "idle but ready" from "actively processing"
    // without learning the integer codes.
    const struct {
        int statusInt;
        const char* expectedName;
    } cases[] = {
        {0, "offline"},
        {1, "online"},
        {2, "processing"},
        {3, "error"},
    };

    for (const auto& c : cases) {
        pub.entries.clear();
        EquipmentStatus es;
        es.equipmentId = 7;
        es.status = c.statusInt;
        es.supplyLevel = 42;
        model.fireEquipment(es);
        ASSERT_EQ(pub.entries.size(), 2U)  // state + supply
            << "status=" << c.statusInt;
        EXPECT_EQ(pub.entries[0].topic,   "test-prefix/equipment/7/state");
        EXPECT_EQ(pub.entries[0].payload, c.expectedName)
            << "status=" << c.statusInt;
    }
}

TEST(ProductionTelemetryBridgeTest, EquipmentStatusAlsoPublishesSupplyLevel) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge bridge(pub, model, makeConfig());
    bridge.wire();

    EquipmentStatus es;
    es.equipmentId = 1;
    es.status = 1;
    es.supplyLevel = 85;
    model.fireEquipment(es);

    ASSERT_EQ(pub.entries.size(), 2U);
    EXPECT_EQ(pub.entries[1].topic,   "test-prefix/equipment/1/supply");
    EXPECT_EQ(pub.entries[1].payload, "85");
}

// Quality

TEST(ProductionTelemetryBridgeTest, QualityCheckpointPublishesPassRate) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge bridge(pub, model, makeConfig());
    bridge.wire();

    QualityCheckpoint qc;
    qc.checkpointId = 5;
    qc.passRate = 98.7f;
    qc.status = 0;
    model.fireQuality(qc);

    // Rate + status = two publishes per checkpoint event.
    ASSERT_EQ(pub.entries.size(), 2U);
    EXPECT_EQ(pub.entries[0].topic,   "test-prefix/quality/5/rate");
    EXPECT_EQ(pub.entries[0].payload, "98.7");
}

TEST(ProductionTelemetryBridgeTest, QualityFormatsRateWithOneDecimal) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge bridge(pub, model, makeConfig());
    bridge.wire();

    QualityCheckpoint qc;
    qc.checkpointId = 1;
    qc.passRate = 100.0f;
    model.fireQuality(qc);

    ASSERT_EQ(pub.entries.size(), 2U);
    EXPECT_EQ(pub.entries[0].payload, "100.0");
}

TEST(ProductionTelemetryBridgeTest, QualityCheckpointPublishesNamedStatus) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge bridge(pub, model, makeConfig());
    bridge.wire();

    const struct {
        int statusInt;
        const char* expectedName;
    } cases[] = {
        {0, "passing"},
        {1, "warning"},
        {2, "critical"},
    };

    for (const auto& c : cases) {
        pub.entries.clear();
        QualityCheckpoint qc;
        qc.checkpointId = 2;
        qc.passRate = 95.0f;
        qc.status = c.statusInt;
        model.fireQuality(qc);
        ASSERT_EQ(pub.entries.size(), 2U)  // rate + status
            << "status=" << c.statusInt;
        EXPECT_EQ(pub.entries[1].topic,   "test-prefix/quality/2/status");
        EXPECT_EQ(pub.entries[1].payload, c.expectedName)
            << "status=" << c.statusInt;
    }
}

// Topic prefix

TEST(ProductionTelemetryBridgeTest, RespectsCustomTopicPrefix) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge bridge(pub, model, makeConfig("factory42/lineA"));
    bridge.wire();

    model.fireSystemState(SystemState::IDLE);
    EquipmentStatus es;
    es.equipmentId = 1;
    es.status = 0;
    model.fireEquipment(es);

    // Equipment event now publishes both /state and /supply -- so
    // a single fire produces 2 entries, plus the 1 system-state
    // entry = 3 total.
    ASSERT_EQ(pub.entries.size(), 3U);
    EXPECT_EQ(pub.entries[0].topic, "factory42/lineA/state");
    EXPECT_EQ(pub.entries[1].topic, "factory42/lineA/equipment/1/state");
    EXPECT_EQ(pub.entries[2].topic, "factory42/lineA/equipment/1/supply");
}

// Lifecycle: bridge before wire() doesn't publish

TEST(ProductionTelemetryBridgeTest, NoPublishIfWireNotCalled) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge bridge(pub, model, makeConfig());
    // Deliberately do NOT call wire().

    model.fireSystemState(SystemState::RUNNING);
    EXPECT_TRUE(pub.entries.empty());
}

// Config flags: JSON-only, dual emit

TEST(ProductionTelemetryBridgeTest, JsonOnlyEmitsOnlyJsonTopics) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge::Config cfg;
    cfg.topicPrefix = "test-prefix";
    cfg.emitPlainText = false;
    cfg.emitJson = true;
    ProductionTelemetryBridge bridge(pub, model, cfg);
    bridge.wire();

    model.fireSystemState(SystemState::RUNNING);
    EquipmentStatus es;
    es.equipmentId = 7;
    es.status = 2;
    es.supplyLevel = 42;
    model.fireEquipment(es);
    QualityCheckpoint qc;
    qc.checkpointId = 5;
    qc.passRate = 98.7f;
    qc.status = 0;
    model.fireQuality(qc);

    // Each event is one publish on the JSON topology (consolidated
    // per entity), so 3 events -> 3 publishes.
    ASSERT_EQ(pub.entries.size(), 3U);
    EXPECT_EQ(pub.entries[0].topic,   "test-prefix/state/json");
    EXPECT_EQ(pub.entries[0].payload, R"({"state":"running"})");
    EXPECT_EQ(pub.entries[1].topic,   "test-prefix/equipment/7/json");
    EXPECT_EQ(pub.entries[1].payload,
              R"({"id":7,"state":"processing","supply":42})");
    EXPECT_EQ(pub.entries[2].topic,   "test-prefix/quality/5/json");
    EXPECT_EQ(pub.entries[2].payload,
              R"({"id":5,"rate":98.7,"status":"passing"})");
}

TEST(ProductionTelemetryBridgeTest, DualEmitProducesBothShapes) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge::Config cfg;
    cfg.topicPrefix = "test-prefix";
    cfg.emitPlainText = true;
    cfg.emitJson = true;
    ProductionTelemetryBridge bridge(pub, model, cfg);
    bridge.wire();

    EquipmentStatus es;
    es.equipmentId = 1;
    es.status = 1;
    es.supplyLevel = 85;
    model.fireEquipment(es);

    // Plain emits /state + /supply (2); JSON emits /json (1). Order
    // matches Config flag order: plain first, then JSON.
    ASSERT_EQ(pub.entries.size(), 3U);
    EXPECT_EQ(pub.entries[0].topic,   "test-prefix/equipment/1/state");
    EXPECT_EQ(pub.entries[0].payload, "online");
    EXPECT_EQ(pub.entries[1].topic,   "test-prefix/equipment/1/supply");
    EXPECT_EQ(pub.entries[1].payload, "85");
    EXPECT_EQ(pub.entries[2].topic,   "test-prefix/equipment/1/json");
    EXPECT_EQ(pub.entries[2].payload,
              R"({"id":1,"state":"online","supply":85})");
}

TEST(ProductionTelemetryBridgeTest, JsonQualityFormatsRateWithOneDecimal) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge::Config cfg;
    cfg.topicPrefix = "test-prefix";
    cfg.emitPlainText = false;
    cfg.emitJson = true;
    ProductionTelemetryBridge bridge(pub, model, cfg);
    bridge.wire();

    QualityCheckpoint qc;
    qc.checkpointId = 1;
    qc.passRate = 100.0f;
    qc.status = 1;
    model.fireQuality(qc);

    ASSERT_EQ(pub.entries.size(), 1U);
    EXPECT_EQ(pub.entries[0].payload,
              R"({"id":1,"rate":100.0,"status":"warning"})");
}

TEST(ProductionTelemetryBridgeTest, NoFormattersMeansNoPublishes) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge::Config cfg;
    cfg.topicPrefix = "test-prefix";
    cfg.emitPlainText = false;
    cfg.emitJson = false;
    ProductionTelemetryBridge bridge(pub, model, cfg);
    bridge.wire();

    model.fireSystemState(SystemState::RUNNING);
    EquipmentStatus es;
    es.equipmentId = 0;
    model.fireEquipment(es);

    // Bridge still subscribes (the callbacks fire), but with no
    // formatters in the list the fan-out has nothing to do. This
    // matches the documented contract: empty list -> silent bridge.
    EXPECT_TRUE(pub.entries.empty());
}
