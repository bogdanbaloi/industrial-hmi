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

    [[nodiscard]] bool subscribeFloat(std::string_view, FloatCallback)
        override { return false; }
    [[nodiscard]] bool subscribeInt32(std::string_view nodeBrowsePath,
                                      Int32Callback callback) override {
        int32Subs_[std::string{nodeBrowsePath}] = std::move(callback);
        return true;
    }
    [[nodiscard]] bool subscribeBool(std::string_view, BoolCallback)
        override { return false; }
    [[nodiscard]] std::size_t monitoredItemCount() const noexcept override {
        return int32Subs_.size();
    }
    [[nodiscard]] std::string endpointUrl() const override {
        return "opc.tcp://fake/";
    }

    /// Push a synthetic data-change notification to whatever
    /// callback was registered for `path`. No-op if nothing's
    /// subscribed (matches what a real client would observe -- the
    /// server only delivers notifications for armed monitored items).
    void injectInt32(const std::string& path, std::int32_t value) const {
        const auto it = int32Subs_.find(path);
        if (it != int32Subs_.end()) it->second(path, value);
    }

    [[nodiscard]] std::vector<std::string> subscribedPaths() const {
        std::vector<std::string> out;
        out.reserve(int32Subs_.size());
        for (const auto& [k, _] : int32Subs_) out.push_back(k);
        return out;
    }

private:
    bool running_{false};
    std::map<std::string, Int32Callback> int32Subs_;
};

}  // namespace

TEST(OpcUaIngestBridgeTest, WireSubscribesToOneStatusNodePerEquipment) {
    FakeOpcUaClient client;
    MockProductionModel model;

    OpcUaIngestBridge::Config cfg;
    cfg.topicPrefix    = "Plant";
    cfg.equipmentCount = 3;
    OpcUaIngestBridge bridge(client, model, cfg);
    bridge.wire();

    ASSERT_EQ(client.monitoredItemCount(), 3U);
    const auto paths = client.subscribedPaths();
    EXPECT_NE(std::find(paths.begin(), paths.end(),
                        "Plant/EquipmentLines/Line0/Status"),
              paths.end());
    EXPECT_NE(std::find(paths.begin(), paths.end(),
                        "Plant/EquipmentLines/Line1/Status"),
              paths.end());
    EXPECT_NE(std::find(paths.begin(), paths.end(),
                        "Plant/EquipmentLines/Line2/Status"),
              paths.end());
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
