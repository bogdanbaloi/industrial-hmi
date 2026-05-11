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

TEST(SensorIngestBridgeTest, WireSubscribesToOneTopicPerEquipmentSlot) {
    FakeSubscriber subscriber;
    MockProductionModel model;

    SensorIngestBridge::Config cfg;
    cfg.topicPrefix    = "sensors";
    cfg.equipmentCount = 3;
    SensorIngestBridge bridge(subscriber, model, cfg);
    bridge.wire();

    EXPECT_EQ(subscriber.size(), 3U);
    const auto topics = subscriber.topics();
    EXPECT_NE(std::find(topics.begin(), topics.end(),
                        "sensors/equipment/0/state"),
              topics.end());
    EXPECT_NE(std::find(topics.begin(), topics.end(),
                        "sensors/equipment/1/state"),
              topics.end());
    EXPECT_NE(std::find(topics.begin(), topics.end(),
                        "sensors/equipment/2/state"),
              topics.end());
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
    SensorIngestBridge bridge(subscriber, model, cfg);
    bridge.wire();

    EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(1);
    subscriber.inject("factory-42/plant-1/equipment/0/state", "1");
}
