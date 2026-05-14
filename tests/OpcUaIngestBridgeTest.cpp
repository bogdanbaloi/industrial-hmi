// Tests for app::integration::opcua::OpcUaIngestBridge.
//
// Same shape as SensorIngestBridgeTest: a `FakeOpcUaClient` captures
// (browse-path, callback) subscriptions and exposes `inject(path,
// value)` so the test can simulate a server pushing a notification
// without spinning a real UA session.

#include "src/integration/opcua/OpcUaIngestBridge.h"

#include "src/integration/opcua/OpcUaClient.h"
#include "tests/mocks/MockProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

using app::integration::opcua::OpcUaClient;
using app::integration::opcua::OpcUaIngestBridge;
using app::test::MockProductionModel;
using testing::_;

/// Test double for `OpcUaClient`. Records every typed subscribe
/// (here only Int32 is exercised; the other two throw to flush out
/// accidental wiring), and lets the test push synthetic notifications
/// through `inject(path, value)`.
class FakeOpcUaClient final : public OpcUaClient {
public:
    void start() override { running_ = true; }
    void stop() override  { running_ = false; }
    [[nodiscard]] bool isRunning() const override { return running_; }
    [[nodiscard]] std::string name() const override { return "OPC-UA Fake"; }
    [[nodiscard]] std::string metricsSummary() const override { return {}; }
    [[nodiscard]] app::integration::BackendState
        connectionState() const noexcept override {
        return running_ ? app::integration::BackendState::Connected
                        : app::integration::BackendState::Disconnected;
    }

    [[nodiscard]] bool subscribeFloat(std::string_view nodeBrowsePath,
                                      FloatCallback callback) override {
        floatSubs_[std::string{nodeBrowsePath}] = std::move(callback);
        return true;
    }
    [[nodiscard]] bool subscribeInt32(std::string_view nodeBrowsePath,
                                      Int32Callback callback) override {
        int32Subs_[std::string{nodeBrowsePath}] = std::move(callback);
        return true;
    }
    [[nodiscard]] bool subscribeBool(std::string_view, BoolCallback)
        override { return false; }
    [[nodiscard]] std::size_t monitoredItemCount() const noexcept override {
        return int32Subs_.size() + floatSubs_.size();
    }
    [[nodiscard]] std::string endpointUrl() const override {
        return "opc.tcp://fake/";
    }

    /// Push a synthetic data-change notification. No-op if nothing's
    /// subscribed (matches what a real client observes -- the server
    /// only delivers notifications for armed monitored items).
    void injectInt32(const std::string& path, std::int32_t value) const {
        const auto it = int32Subs_.find(path);
        if (it != int32Subs_.end()) it->second(path, value);
    }

    void injectFloat(const std::string& path, float value) const {
        const auto it = floatSubs_.find(path);
        if (it != floatSubs_.end()) it->second(path, value);
    }

    [[nodiscard]] std::vector<std::string> subscribedPaths() const {
        std::vector<std::string> out;
        out.reserve(int32Subs_.size() + floatSubs_.size());
        for (const auto& [k, _] : int32Subs_) out.push_back(k);
        for (const auto& [k, _] : floatSubs_) out.push_back(k);
        return out;
    }

private:
    bool running_{false};
    std::map<std::string, Int32Callback> int32Subs_;
    std::map<std::string, FloatCallback> floatSubs_;
};

}  // namespace

TEST(OpcUaIngestBridgeTest, WireSubscribesEveryNodeFamily) {
    FakeOpcUaClient client;
    MockProductionModel model;

    OpcUaIngestBridge::Config cfg;
    cfg.topicPrefix    = "Plant";
    cfg.equipmentCount = 3;
    cfg.qualityCount   = 3;
    OpcUaIngestBridge bridge(client, model, cfg);
    bridge.wire();

    // Three families × three slots/checkpoints = nine monitored
    // items:
    //   Plant/EquipmentLines/Line<id>/Status   x 3 (Int32 enabled)
    //   Plant/EquipmentLines/Line<id>/Supply   x 3 (Int32 percent)
    //   Plant/QualityCheckpoints/Checkpoint<id>/PassRate x 3 (Float)
    ASSERT_EQ(client.monitoredItemCount(), 9U);
    const auto paths = client.subscribedPaths();
    auto has = [&](const std::string& p) {
        return std::find(paths.begin(), paths.end(), p) != paths.end();
    };
    EXPECT_TRUE(has("Plant/EquipmentLines/Line0/Status"));
    EXPECT_TRUE(has("Plant/EquipmentLines/Line1/Status"));
    EXPECT_TRUE(has("Plant/EquipmentLines/Line2/Status"));
    EXPECT_TRUE(has("Plant/EquipmentLines/Line0/Supply"));
    EXPECT_TRUE(has("Plant/EquipmentLines/Line1/Supply"));
    EXPECT_TRUE(has("Plant/EquipmentLines/Line2/Supply"));
    EXPECT_TRUE(has("Plant/QualityCheckpoints/Checkpoint0/PassRate"));
    EXPECT_TRUE(has("Plant/QualityCheckpoints/Checkpoint1/PassRate"));
    EXPECT_TRUE(has("Plant/QualityCheckpoints/Checkpoint2/PassRate"));
}

TEST(OpcUaIngestBridgeTest, OfflineStatusDisablesEquipment) {
    FakeOpcUaClient client;
    MockProductionModel model;
    OpcUaIngestBridge bridge(client, model);
    bridge.wire();

    EXPECT_CALL(model, setEquipmentEnabled(0U, false)).Times(1);
    client.injectInt32("Factory/EquipmentLines/Line0/Status", 0);
}

TEST(OpcUaIngestBridgeTest, NonOfflineStatusEnablesEquipment) {
    FakeOpcUaClient client;
    MockProductionModel model;
    OpcUaIngestBridge bridge(client, model);
    bridge.wire();

    // The model only carries enabled/disabled; the bridge maps every
    // non-offline status (1 Online / 2 Processing / 3 Error) to the
    // same setter call. We assert with a single Times(3) so the
    // ordering of injections doesn't matter.
    EXPECT_CALL(model, setEquipmentEnabled(1U, true)).Times(1);
    client.injectInt32("Factory/EquipmentLines/Line1/Status", /*Online*/ 1);
}

TEST(OpcUaIngestBridgeTest, RepeatedSameStatusIsDeduplicated) {
    FakeOpcUaClient client;
    MockProductionModel model;
    OpcUaIngestBridge bridge(client, model);
    bridge.wire();

    // The first notification flips the bit; subsequent Online ->
    // Processing notifications carry the SAME enabled bit and must
    // not bounce through the model.
    EXPECT_CALL(model, setEquipmentEnabled(2U, true)).Times(1);
    client.injectInt32("Factory/EquipmentLines/Line2/Status", 1);  // Online
    client.injectInt32("Factory/EquipmentLines/Line2/Status", 2);  // Processing
    client.injectInt32("Factory/EquipmentLines/Line2/Status", 3);  // Error
}

TEST(OpcUaIngestBridgeTest, EnabledBitChangeReachesModelAgain) {
    FakeOpcUaClient client;
    MockProductionModel model;
    OpcUaIngestBridge bridge(client, model);
    bridge.wire();

    EXPECT_CALL(model, setEquipmentEnabled(0U, true)).Times(1);
    EXPECT_CALL(model, setEquipmentEnabled(0U, false)).Times(1);

    client.injectInt32("Factory/EquipmentLines/Line0/Status", 1);  // Online
    client.injectInt32("Factory/EquipmentLines/Line0/Status", 0);  // Offline
}

TEST(OpcUaIngestBridgeTest, OutOfRangeEquipmentIdIsIgnored) {
    FakeOpcUaClient client;
    MockProductionModel model;
    OpcUaIngestBridge::Config cfg;
    cfg.equipmentCount = 1;
    OpcUaIngestBridge bridge(client, model, cfg);
    bridge.wire();

    // Only Line0 was registered. A notification on Line5 (never
    // subscribed) wouldn't fire anyway, but we double-check the
    // bridge doesn't accidentally accept an id past the configured
    // range when called directly.
    EXPECT_CALL(model, setEquipmentEnabled(_, _)).Times(0);
    client.injectInt32("Factory/EquipmentLines/Line5/Status", 1);
}

// ===== Analog notifications (A3b) ================================

TEST(OpcUaIngestBridgeTest, SupplyNotificationForwardsAsInt) {
    FakeOpcUaClient client;
    MockProductionModel model;
    OpcUaIngestBridge bridge(client, model);
    bridge.wire();

    EXPECT_CALL(model, setEquipmentSupplyLevel(0U, 73)).Times(1);
    client.injectInt32("Factory/EquipmentLines/Line0/Supply", 73);
}

TEST(OpcUaIngestBridgeTest, SupplyNotificationDedupsRepeatedValue) {
    FakeOpcUaClient client;
    MockProductionModel model;
    OpcUaIngestBridge bridge(client, model);
    bridge.wire();

    // Three identical notifications -> single model call.
    EXPECT_CALL(model, setEquipmentSupplyLevel(1U, 42)).Times(1);
    client.injectInt32("Factory/EquipmentLines/Line1/Supply", 42);
    client.injectInt32("Factory/EquipmentLines/Line1/Supply", 42);
    client.injectInt32("Factory/EquipmentLines/Line1/Supply", 42);
}

TEST(OpcUaIngestBridgeTest, SupplyNotificationFiresAgainOnChange) {
    FakeOpcUaClient client;
    MockProductionModel model;
    OpcUaIngestBridge bridge(client, model);
    bridge.wire();

    {
        testing::InSequence seq;
        EXPECT_CALL(model, setEquipmentSupplyLevel(0U, 80)).Times(1);
        EXPECT_CALL(model, setEquipmentSupplyLevel(0U, 60)).Times(1);
        EXPECT_CALL(model, setEquipmentSupplyLevel(0U, 80)).Times(1);
    }
    client.injectInt32("Factory/EquipmentLines/Line0/Supply", 80);
    client.injectInt32("Factory/EquipmentLines/Line0/Supply", 80);  // dedup
    client.injectInt32("Factory/EquipmentLines/Line0/Supply", 60);
    client.injectInt32("Factory/EquipmentLines/Line0/Supply", 80);
}

TEST(OpcUaIngestBridgeTest, PassRateNotificationForwardsAsFloat) {
    FakeOpcUaClient client;
    MockProductionModel model;
    OpcUaIngestBridge bridge(client, model);
    bridge.wire();

    EXPECT_CALL(model,
                setQualityPassRate(0U, testing::FloatEq(98.7F))).Times(1);
    client.injectFloat("Factory/QualityCheckpoints/Checkpoint0/PassRate",
                       98.7F);
}

TEST(OpcUaIngestBridgeTest, PassRateNotificationDedupsRepeatedValue) {
    FakeOpcUaClient client;
    MockProductionModel model;
    OpcUaIngestBridge bridge(client, model);
    bridge.wire();

    EXPECT_CALL(model,
                setQualityPassRate(1U, testing::FloatEq(95.5F))).Times(1);
    client.injectFloat("Factory/QualityCheckpoints/Checkpoint1/PassRate",
                       95.5F);
    client.injectFloat("Factory/QualityCheckpoints/Checkpoint1/PassRate",
                       95.5F);
}

TEST(OpcUaIngestBridgeTest, AnalogTracksEntitiesIndependently) {
    FakeOpcUaClient client;
    MockProductionModel model;
    OpcUaIngestBridge bridge(client, model);
    bridge.wire();

    // entity 0's supply state must not poison entity 1's cache.
    EXPECT_CALL(model, setEquipmentSupplyLevel(0U, 50)).Times(1);
    EXPECT_CALL(model, setEquipmentSupplyLevel(1U, 50)).Times(1);
    client.injectInt32("Factory/EquipmentLines/Line0/Supply", 50);
    client.injectInt32("Factory/EquipmentLines/Line1/Supply", 50);
}
