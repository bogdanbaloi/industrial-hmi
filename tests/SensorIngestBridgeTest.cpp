// Tests for app::integration::SensorIngestBridge.
//
// The bridge has one job: take inbound (topic, payload) pairs from a
// TelemetrySubscriber and turn them into ProductionModel mutations.
// We pair it with:
//
//   * FakeSubscriber  -- captures registered (filter, callback) pairs,
//     exposes `inject(topic, payload)` so the test can simulate a
//     broker delivering a frame without spinning a real MQTT loop.
//   * MockProductionModel -- gmock-driven; lets us assert that the
//     bridge calls setEquipmentEnabled with the right id + value.
//
// No socket, no MQTT, no threads -- the bridge under test is pure
// callback plumbing.

#include "src/integration/SensorIngestBridge.h"

#include "src/integration/TelemetrySubscriber.h"
#include "tests/mocks/MockProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

using app::integration::SensorIngestBridge;
using app::integration::TelemetrySubscriber;
using app::test::MockProductionModel;
using testing::_;

/// Test double for TelemetrySubscriber. Records subscriptions in a
/// `topic -> callback` map and lets the test drive the callbacks
/// directly via `inject()`.
class FakeSubscriber final : public TelemetrySubscriber {
public:
    void subscribe(const std::string& topicFilter,
                   MessageCallback callback) override {
        subscriptions_[topicFilter] = std::move(callback);
    }

    /// Push a (topic, payload) pair through any matching subscription.
    /// Mirrors what an MQTT broker would do over the wire.
    void inject(const std::string& topic, std::string_view payload) const {
        const auto it = subscriptions_.find(topic);
        if (it != subscriptions_.end()) {
            it->second(topic, payload);
        }
    }

    [[nodiscard]] std::vector<std::string> topics() const {
        std::vector<std::string> out;
        out.reserve(subscriptions_.size());
        for (const auto& [k, _] : subscriptions_) out.push_back(k);
        return out;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return subscriptions_.size();
    }

private:
    std::map<std::string, MessageCallback> subscriptions_;
};

}  // namespace

TEST(SensorIngestBridgeTest, WireSubscribesEveryTopicFamilyPerSlot) {
    FakeSubscriber subscriber;
    MockProductionModel model;

    SensorIngestBridge::Config cfg;
    cfg.topicPrefix    = "sensors";
    cfg.equipmentCount = 3;
    cfg.qualityCount   = 3;
    SensorIngestBridge bridge(subscriber, model, cfg);
    bridge.wire();

    // Three topic families × three slots/checkpoints = nine
    // subscriptions:
    //   state   x equipmentCount (boolean enabled)
    //   supply  x equipmentCount (int supply level)
    //   rate    x qualityCount   (float pass rate)
    EXPECT_EQ(subscriber.size(), 9U);
    const auto topics = subscriber.topics();
    auto has = [&](const std::string& t) {
        return std::find(topics.begin(), topics.end(), t) != topics.end();
    };
    EXPECT_TRUE(has("sensors/equipment/0/state"));
    EXPECT_TRUE(has("sensors/equipment/1/state"));
    EXPECT_TRUE(has("sensors/equipment/2/state"));
    EXPECT_TRUE(has("sensors/equipment/0/supply"));
    EXPECT_TRUE(has("sensors/equipment/1/supply"));
    EXPECT_TRUE(has("sensors/equipment/2/supply"));
    EXPECT_TRUE(has("sensors/quality/0/rate"));
    EXPECT_TRUE(has("sensors/quality/1/rate"));
    EXPECT_TRUE(has("sensors/quality/2/rate"));
}

TEST(SensorIngestBridgeTest, OnPayloadFlipsCorrespondingEquipmentEnabled) {
    FakeSubscriber subscriber;
    MockProductionModel model;
    SensorIngestBridge bridge(subscriber, model);
    bridge.wire();

    // The bridge must dispatch to equipment id 0 with enabled=true
    // when the matching topic publishes "on".
    EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(1);
    subscriber.inject("industrial-hmi-sensors/equipment/0/state", "on");
}

TEST(SensorIngestBridgeTest, OffPayloadFlipsCorrespondingEquipmentDisabled) {
    FakeSubscriber subscriber;
    MockProductionModel model;
    SensorIngestBridge bridge(subscriber, model);
    bridge.wire();

    EXPECT_CALL(model, setEquipmentEnabled(1U, false)).Times(1);
    subscriber.inject("industrial-hmi-sensors/equipment/1/state", "off");
}

TEST(SensorIngestBridgeTest, AcceptsBooleanAndNumericAliasesCaseInsensitive) {
    FakeSubscriber subscriber;
    MockProductionModel model;
    SensorIngestBridge bridge(subscriber, model);
    bridge.wire();

    // Each accepted spelling must drive the same setEquipmentEnabled
    // call. Five truthy aliases, five falsy ones.
    EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(4);
    EXPECT_CALL(model, setEquipmentEnabled(0U, false)).Times(4);

    const char* on[]  = {"on", "ON", "1", "True"};
    const char* off[] = {"off", "OFF", "0", "False"};
    for (const auto* p : on) {
        subscriber.inject("industrial-hmi-sensors/equipment/0/state", p);
    }
    for (const auto* p : off) {
        subscriber.inject("industrial-hmi-sensors/equipment/0/state", p);
    }
}

TEST(SensorIngestBridgeTest, TrimsWhitespaceAroundPayload) {
    FakeSubscriber subscriber;
    MockProductionModel model;
    SensorIngestBridge bridge(subscriber, model);
    bridge.wire();

    EXPECT_CALL(model, setEquipmentEnabled(2U, true)).Times(1);
    subscriber.inject("industrial-hmi-sensors/equipment/2/state", "  on\n");
}

TEST(SensorIngestBridgeTest, UnknownPayloadIsDroppedSilently) {
    FakeSubscriber subscriber;
    MockProductionModel model;
    SensorIngestBridge bridge(subscriber, model);
    bridge.wire();

    // The bridge is a fan-in, not a validator: a misbehaving sensor
    // must not be able to crash or no-op the HMI by sending garbage.
    EXPECT_CALL(model, setEquipmentEnabled(_, _)).Times(0);
    subscriber.inject("industrial-hmi-sensors/equipment/0/state", "garbage");
    subscriber.inject("industrial-hmi-sensors/equipment/0/state", "");
    subscriber.inject("industrial-hmi-sensors/equipment/0/state", "yes please");
}

TEST(SensorIngestBridgeTest, RespectsCustomTopicPrefix) {
    FakeSubscriber subscriber;
    MockProductionModel model;
    SensorIngestBridge::Config cfg;
    cfg.topicPrefix    = "factory-42/plant-1";
    cfg.equipmentCount = 1;
    cfg.qualityCount   = 1;
    SensorIngestBridge bridge(subscriber, model, cfg);
    bridge.wire();

    EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(1);
    subscriber.inject("factory-42/plant-1/equipment/0/state", "1");
}

// ===== Analog payloads (A3b) =====================================

TEST(SensorIngestBridgeTest, SupplyPayloadParsesIntegerPercent) {
    FakeSubscriber subscriber;
    MockProductionModel model;
    SensorIngestBridge bridge(subscriber, model);
    bridge.wire();

    EXPECT_CALL(model, setEquipmentSupplyLevel(0U, 73)).Times(1);
    subscriber.inject("industrial-hmi-sensors/equipment/0/supply", "73");
}

TEST(SensorIngestBridgeTest, SupplyPayloadTolerantesWhitespace) {
    FakeSubscriber subscriber;
    MockProductionModel model;
    SensorIngestBridge bridge(subscriber, model);
    bridge.wire();

    EXPECT_CALL(model, setEquipmentSupplyLevel(1U, 42)).Times(1);
    subscriber.inject("industrial-hmi-sensors/equipment/1/supply",
                      "  42\n");
}

TEST(SensorIngestBridgeTest, SupplyPayloadAcceptsNegative) {
    // Parser accepts the value; the model clamps. Verifies the
    // parser doesn't pre-filter values the model considers
    // out-of-domain (clamp is the model's job, not the bridge's).
    FakeSubscriber subscriber;
    MockProductionModel model;
    SensorIngestBridge bridge(subscriber, model);
    bridge.wire();

    EXPECT_CALL(model, setEquipmentSupplyLevel(0U, -5)).Times(1);
    subscriber.inject("industrial-hmi-sensors/equipment/0/supply", "-5");
}

TEST(SensorIngestBridgeTest, SupplyPayloadRejectsGarbage) {
    FakeSubscriber subscriber;
    MockProductionModel model;
    SensorIngestBridge bridge(subscriber, model);
    bridge.wire();

    EXPECT_CALL(model, setEquipmentSupplyLevel(_, _)).Times(0);
    subscriber.inject("industrial-hmi-sensors/equipment/0/supply", "abc");
    subscriber.inject("industrial-hmi-sensors/equipment/0/supply", "42abc");
    subscriber.inject("industrial-hmi-sensors/equipment/0/supply", "");
    subscriber.inject("industrial-hmi-sensors/equipment/0/supply", "  ");
}

TEST(SensorIngestBridgeTest, RatePayloadParsesFloatPercent) {
    FakeSubscriber subscriber;
    MockProductionModel model;
    SensorIngestBridge bridge(subscriber, model);
    bridge.wire();

    EXPECT_CALL(model, setQualityPassRate(0U, testing::FloatEq(98.7F)))
        .Times(1);
    subscriber.inject("industrial-hmi-sensors/quality/0/rate", "98.7");
}

TEST(SensorIngestBridgeTest, RatePayloadAcceptsInteger) {
    // "95" without a decimal point should still parse as 95.0f.
    FakeSubscriber subscriber;
    MockProductionModel model;
    SensorIngestBridge bridge(subscriber, model);
    bridge.wire();

    EXPECT_CALL(model, setQualityPassRate(2U, testing::FloatEq(95.0F)))
        .Times(1);
    subscriber.inject("industrial-hmi-sensors/quality/2/rate", "95");
}

TEST(SensorIngestBridgeTest, RatePayloadRejectsGarbage) {
    FakeSubscriber subscriber;
    MockProductionModel model;
    SensorIngestBridge bridge(subscriber, model);
    bridge.wire();

    EXPECT_CALL(model, setQualityPassRate(_, _)).Times(0);
    subscriber.inject("industrial-hmi-sensors/quality/0/rate", "nope");
    subscriber.inject("industrial-hmi-sensors/quality/0/rate", "98.7abc");
    subscriber.inject("industrial-hmi-sensors/quality/0/rate", "");
}
