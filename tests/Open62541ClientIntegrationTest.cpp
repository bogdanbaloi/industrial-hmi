// End-to-end integration test for Open62541Client.
//
// Spins up the real `Open62541Server` to host an address space, then
// spins our `Open62541Client` against it over loopback. The client
// subscribes to a typed variable; the server writes a fresh value;
// the test asserts the typed callback fires with the right payload.
//
// Anything that breaks the inbound wire (subscription create,
// monitored item registration, variant tagging on the decode side)
// surfaces here -- the unit tests stop at the abstract interface.
//
// Why a separate executable: this test DOES link open62541. The
// mock-based unit tests stay open62541-free so they keep running on
// a build without BUILD_OPCUA_BACKEND.

#include "src/core/LoggerImpl.h"
#include "src/integration/opcua/Open62541Client.h"
#include "src/integration/opcua/Open62541Server.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using app::integration::opcua::Open62541Client;
using app::integration::opcua::Open62541Server;
using app::integration::opcua::OpcUaConfig;

namespace {

/// Fresh high port avoiding the IANA OPC-UA defaults so we don't
/// collide with a live PLC on the developer's box. Matches the
/// rationale in `Open62541ServerIntegrationTest`.
constexpr std::uint16_t kTestPort = 14841;

/// Server-side publishing interval drives notification latency.
/// Pick something short so the test doesn't wait forever, but
/// long enough that open62541's own scheduling lands inside it.
constexpr double kPublishingIntervalMs = 100.0;

/// How long the test waits for the inbound notification once the
/// server has written a value. Generous because the server's
/// publish loop + the client's iterate cadence + scheduler jitter
/// all stack; tightening this is the first thing to revisit if the
/// suite starts flaking.
constexpr std::chrono::milliseconds kDeliveryTimeout{2000};

class Open62541ClientIntegrationTest : public ::testing::Test {
protected:
    Open62541ClientIntegrationTest()
        : logger_{std::make_unique<app::core::ConsoleLogger>()} {
        // Server -- exposes a "Sensor/Reading" Float variable that
        // the test will write to and the client will observe.
        OpcUaConfig serverCfg;
        serverCfg.port = kTestPort;
        serverCfg.applicationUri  = "urn:test:opcua-client-int";
        serverCfg.applicationName = "OPC-UA Client Integration Test";
        server_ = std::make_unique<Open62541Server>(std::move(serverCfg),
                                                    logger_);

        Open62541Client::Config clientCfg;
        clientCfg.endpointUrl = "opc.tcp://127.0.0.1:" +
                                std::to_string(kTestPort);
        clientCfg.publishingIntervalMs = kPublishingIntervalMs;
        client_ = std::make_unique<Open62541Client>(std::move(clientCfg),
                                                    logger_);
    }

    ~Open62541ClientIntegrationTest() override {
        // Tear down in reverse order: client first so its subscribe
        // delete doesn't race the server going away.
        if (client_) client_->stop();
        if (server_) server_->stop();
    }

    app::core::Logger logger_;
    std::unique_ptr<Open62541Server> server_;
    std::unique_ptr<Open62541Client> client_;
};

}  // namespace

TEST_F(Open62541ClientIntegrationTest,
       FloatNotificationPropagatesFromServerWriteToClientCallback) {
    // 1. Bring the server up + create the variable.
    ASSERT_NO_THROW(server_->start());
    ASSERT_TRUE(server_->isRunning());
    ASSERT_TRUE(server_->addObject("Sensor"));
    ASSERT_TRUE(server_->addFloatVariable("Sensor/Reading", 0.0F));

    // 2. Subscribe BEFORE start so the client's replay path is on
    //    the hot path -- mirrors how `main.cpp` wires bridges.
    std::atomic<int> hitCount{0};
    std::mutex captureMutex;
    std::string capturedPath;
    float       capturedValue{0.0F};

    ASSERT_TRUE(client_->subscribeFloat(
        "Sensor/Reading",
        [&](std::string_view path, float value) {
            const std::scoped_lock lock(captureMutex);
            capturedPath.assign(path);
            capturedValue = value;
            hitCount.fetch_add(1, std::memory_order_release);
        }));

    // 3. Connect the client. start() runs the CONNECT handshake,
    //    creates the subscription, replays the queued monitored
    //    item -- all synchronously on the test thread.
    ASSERT_NO_THROW(client_->start());
    EXPECT_TRUE(client_->isRunning());
    EXPECT_EQ(client_->monitoredItemCount(), 1U);

    // 4. Push a known value through the server's typed write. The
    //    client's worker thread should drain a Publish response
    //    carrying the data-change notification.
    constexpr float kExpected = 73.25F;
    ASSERT_TRUE(server_->writeFloat("Sensor/Reading", kExpected));

    // 5. Poll for the post-write notification under a deadline.
    //    open62541 sends the initial value (0.0F) the moment the
    //    monitored item is created, so a `hitCount >= 1` exit
    //    condition would race against the actual change we care
    //    about; we wait specifically for `capturedValue == kExpected`.
    auto sawExpected = [&]() {
        const std::scoped_lock lock(captureMutex);
        return capturedValue == kExpected;
    };
    const auto deadline =
        std::chrono::steady_clock::now() + kDeliveryTimeout;
    while (!sawExpected() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    EXPECT_GE(hitCount.load(std::memory_order_acquire), 1)
        << "client never received a data-change notification within "
        << kDeliveryTimeout.count() << " ms";
    {
        const std::scoped_lock lock(captureMutex);
        EXPECT_EQ(capturedPath, "Sensor/Reading");
        EXPECT_FLOAT_EQ(capturedValue, kExpected)
            << "callback fired but never with the post-write value; "
            << "last seen " << capturedValue;
    }
}

TEST_F(Open62541ClientIntegrationTest,
       ConnectionStateReflectsLifecycle) {
    using app::integration::BackendState;

    // Pre-start: nothing connected, no monitored items.
    EXPECT_EQ(client_->connectionState(), BackendState::Disconnected);

    // Bring up the server first so the client has something to dial.
    server_->start();
    client_->start();
    EXPECT_TRUE(client_->isRunning());
    EXPECT_EQ(client_->connectionState(), BackendState::Connected);

    client_->stop();
    EXPECT_FALSE(client_->isRunning());
    // No monitored items were armed in this case -> Disconnected
    // (not Degraded, which requires at least one prior monitored item).
    EXPECT_EQ(client_->connectionState(), BackendState::Disconnected);
}

TEST_F(Open62541ClientIntegrationTest,
       SubscribeAfterStartArmsMonitoredItem) {
    // Reverse of the first test: connect first, then subscribe.
    server_->start();
    ASSERT_TRUE(server_->addObject("Sensor"));
    ASSERT_TRUE(server_->addInt32Variable("Sensor/Count", 0));
    client_->start();
    ASSERT_TRUE(client_->isRunning());
    ASSERT_EQ(client_->monitoredItemCount(), 0U);

    std::atomic<int> hits{0};
    ASSERT_TRUE(client_->subscribeInt32(
        "Sensor/Count",
        [&](std::string_view, std::int32_t) {
            hits.fetch_add(1, std::memory_order_release);
        }));
    EXPECT_EQ(client_->monitoredItemCount(), 1U);

    constexpr std::int32_t kValue = 7;
    ASSERT_TRUE(server_->writeInt32("Sensor/Count", kValue));

    const auto deadline =
        std::chrono::steady_clock::now() + kDeliveryTimeout;
    while (hits.load(std::memory_order_acquire) == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_GE(hits.load(std::memory_order_acquire), 1);
}
