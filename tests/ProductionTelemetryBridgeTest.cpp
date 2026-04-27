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

TEST(ProductionTelemetryBridgeTest, EquipmentNonErrorStatusMapsToOk) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge bridge(pub, model, makeConfig());
    bridge.wire();

    EquipmentStatus es;
    es.equipmentId = 7;
    es.status = 1;  // processing
    model.fireEquipment(es);

    ASSERT_EQ(pub.entries.size(), 1U);
    EXPECT_EQ(pub.entries[0].topic,   "test-prefix/equipment/7/state");
    EXPECT_EQ(pub.entries[0].payload, "ok");
}

TEST(ProductionTelemetryBridgeTest, EquipmentErrorStatusMapsToFault) {
    CapturingPublisher pub;
    CapturingProductionModel model;
    ProductionTelemetryBridge bridge(pub, model, makeConfig());
    bridge.wire();

    EquipmentStatus es;
    es.equipmentId = 42;
    es.status = 3;  // error
    model.fireEquipment(es);

    ASSERT_EQ(pub.entries.size(), 1U);
    EXPECT_EQ(pub.entries[0].topic,   "test-prefix/equipment/42/state");
    EXPECT_EQ(pub.entries[0].payload, "fault");
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
    model.fireQuality(qc);

    ASSERT_EQ(pub.entries.size(), 1U);
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

    ASSERT_EQ(pub.entries.size(), 1U);
    EXPECT_EQ(pub.entries[0].payload, "100.0");
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

    ASSERT_EQ(pub.entries.size(), 2U);
    EXPECT_EQ(pub.entries[0].topic, "factory42/lineA/state");
    EXPECT_EQ(pub.entries[1].topic, "factory42/lineA/equipment/1/state");
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
