// [utest->req~integration-002~1]
// [utest->req~integration-003~1]
// Covers REQ-INTEGRATION-002 (MQTT sensor ingest) + REQ-INTEGRATION-003
// (Modbus ingest), at the bridge->model seam with a REAL model.
//
// SensorIngestBridgeTest / ModbusIngestBridgeTest both drive a
// MockProductionModel and only assert the bridge *calls* the right
// setter. This wires each bridge to a REAL MirrorModel and asserts the
// inbound value actually mutates the model's state AND re-emits through
// the model's own observer dispatch (the clamp + dedup + fan-out chain
// the mocks stub out). MirrorModel (non-singleton) keeps it isolated.

#include "src/integration/SensorIngestBridge.h"
#include "src/integration/TelemetrySubscriber.h"
#include "src/integration/modbus/ModbusIngestBridge.h"
#include "src/integration/modbus/ModbusRegisterMap.h"
#include "src/model/MirrorModel.h"
#include "src/model/ProductionTypes.h"

#include <map>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using app::integration::SensorIngestBridge;
using app::integration::TelemetrySubscriber;
using app::integration::modbus::ModbusIngestBridge;
using app::integration::modbus::RegisterMapping;
using app::integration::modbus::RegisterType;
using app::model::EquipmentStatus;
using app::model::MirrorModel;

/// Minimal TelemetrySubscriber double: records subscriptions and lets
/// the test deliver a frame, exactly as the MQTT broker would.
class FakeSubscriber final : public TelemetrySubscriber {
public:
    void subscribe(const std::string& topicFilter,
                   MessageCallback callback) override {
        subs_[topicFilter] = std::move(callback);
    }
    void inject(const std::string& topic, std::string_view payload) const {
        const auto it = subs_.find(topic);
        if (it != subs_.end()) it->second(topic, payload);
    }

private:
    std::map<std::string, MessageCallback> subs_;
};

// --- MQTT ingest -> real model --------------------------------------

TEST(IngestBridgeRealModelIntegrationTest, MqttRatePayloadMutatesRealModel) {
    FakeSubscriber subscriber;
    MirrorModel    model;
    SensorIngestBridge::Config cfg;
    cfg.topicPrefix    = "sensors";
    cfg.equipmentCount = 3;
    cfg.qualityCount   = 3;
    SensorIngestBridge bridge(subscriber, model, cfg);
    bridge.wire();

    subscriber.inject("sensors/quality/1/rate", "88.5");

    // Real model state changed (not just "setter was called").
    EXPECT_FLOAT_EQ(model.getQualityCheckpoint(1).passRate, 88.5F);
}

TEST(IngestBridgeRealModelIntegrationTest, MqttSupplyReEmitsThroughModel) {
    FakeSubscriber subscriber;
    MirrorModel    model;
    SensorIngestBridge::Config cfg;
    cfg.topicPrefix    = "sensors";
    cfg.equipmentCount = 3;
    cfg.qualityCount   = 3;
    SensorIngestBridge bridge(subscriber, model, cfg);
    bridge.wire();

    // A downstream observer (e.g. a dashboard) must see the re-emitted
    // event -- proves the full ingest -> model -> observer chain.
    EquipmentStatus seen;
    int calls = 0;
    model.onEquipmentStatusChanged([&](const EquipmentStatus& s) {
        seen = s;
        ++calls;
    });

    subscriber.inject("sensors/equipment/2/supply", "73");

    EXPECT_GE(calls, 1);
    EXPECT_EQ(seen.equipmentId, 2U);
    EXPECT_EQ(seen.supplyLevel, 73);
}

// --- Modbus ingest -> real model ------------------------------------

namespace {
RegisterMapping supplyMapping(std::uint32_t entityId, float scale) {
    RegisterMapping m;
    m.slaveId  = 1;
    m.type     = RegisterType::HoldingRegister;
    m.address  = 0x0010;
    m.field    = app::integration::modbus::FieldKind::EquipmentSupplyLevel;
    m.entityId = entityId;
    m.scale    = scale;
    return m;
}
}  // namespace

TEST(IngestBridgeRealModelIntegrationTest, ModbusRegisterMutatesRealModel) {
    MirrorModel        model;
    ModbusIngestBridge bridge(model);

    EquipmentStatus seen;
    int calls = 0;
    model.onEquipmentStatusChanged([&](const EquipmentStatus& s) {
        seen = s;
        ++calls;
    });

    // Direct-percent PLC (scale 1.0): raw 73 -> 73%.
    bridge.onRegisterChanged(supplyMapping(0, 1.0F), 73);
    EXPECT_GE(calls, 1);
    EXPECT_EQ(seen.equipmentId, 0U);
    EXPECT_EQ(seen.supplyLevel, 73);

    // Fixed-point PLC (scale 0.1): raw 850 -> 85%.
    bridge.onRegisterChanged(supplyMapping(1, 0.1F), 850);
    EXPECT_EQ(seen.equipmentId, 1U);
    EXPECT_EQ(seen.supplyLevel, 85);
}

}  // namespace
