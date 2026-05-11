#pragma once

#include "src/integration/IntegrationBackend.h"
#include "src/integration/TelemetryPublisher.h"
#include "src/integration/TelemetrySubscriber.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Forward-declare Asio types so the header doesn't pull <boost/asio.hpp>
// into every translation unit that includes us. Foreign API naming --
// we don't get to rename Boost's io_context to CamelCase.
namespace boost::asio {
    // NOLINTNEXTLINE(readability-identifier-naming)
    class io_context;
}

namespace app::integration {

/// MQTT 3.1.1 client backend (domain-agnostic, full duplex).
///
/// Single TCP connection to a broker; carries BOTH outbound PUBLISH
/// (via the TelemetryPublisher interface, used by bridges that ship
/// model events out) AND inbound SUBSCRIBE/PUBLISH (via the
/// TelemetrySubscriber interface, used by ingestion bridges that
/// translate broker traffic back into Model mutations).
///
/// One connection rather than two separate clients matches what
/// real-world libraries do (paho, AsyncMQTT5, mosquitto-c) -- MQTT was
/// designed duplex; opening two sockets to the same broker would
/// double the keep-alive overhead and confuse operators who'd see two
/// "MQTT" pills in the I/O panel.
///
/// Separation of concerns lives at the bridge layer instead:
///
///   ProductionTelemetryBridge --> TelemetryPublisher  (outbound)
///   SensorIngestBridge        --> TelemetrySubscriber (inbound)
///                                        |
///                                        v
///                                    MqttClient (one socket)
///
/// Wire format implemented by hand in MqttPacket.h (~400 lines, no
/// system library dependency). MQTT 3.1.1, QoS 0 only -- no PUBACK /
/// PUBREC retry tracking.
///
/// SOLID:
///   * S -- one job: speak MQTT to a broker. No business logic, no
///     domain awareness, no format conversion.
///   * O -- new topic schemas land in their own bridge classes; this
///     publisher stays untouched.
///   * L -- substitutable for any TelemetryPublisher and any
///     IntegrationBackend.
///   * I -- inherits from two narrow interfaces; doesn't expose more.
///   * D -- callers depend on TelemetryPublisher&; this concrete is
///     selected once at composition root (main.cpp).
///
/// Threading:
///   * Owns its own io_context + std::jthread, isolated from
///     ModelContext and any other backend's loops.
///   * publish() is safe from any thread; it posts to the io_context
///     so socket writes serialise on the worker thread.
///
/// Failure handling:
///   * `start()` blocks while it dials the broker and reads CONNACK
///     so DNS / ECONNREFUSED / IDENTIFIER_REJECTED surface as
///     exceptions caught by IntegrationManager::startAll().
///   * Mid-session network drops are NOT auto-reconnected in this PR
///     (kept narrow on purpose). canPublish() flips to false; future
///     publish() calls short-circuit; operator restart or a future
///     reconnect timer reactivates.
/// IANA-assigned MQTT plain-TCP port (RFC 7672 / mosquitto.conf default).
inline constexpr std::uint16_t kDefaultMqttBrokerPort = 1883;

/// Default keep-alive: 60 s is the mosquitto / paho default and a
/// reasonable balance between liveness detection and heartbeat traffic.
inline constexpr std::chrono::seconds kDefaultMqttKeepAlive{60};

/// Default heartbeat (PINGREQ) cadence. Shorter than keep-alive so
/// the broker doesn't hit a timeout between pings. 5 s is a common
/// choice for industrial telemetry on a stable LAN.
inline constexpr std::chrono::milliseconds kDefaultMqttHeartbeatInterval{5000};

class MqttClient : public IntegrationBackend,
                   public TelemetryPublisher,
                   public TelemetrySubscriber {
public:
    /// All knobs in one place so caller wiring stays declarative.
    /// Defaults track typical broker setups (mosquitto on localhost).
    struct Config {
        std::string brokerHost{"127.0.0.1"};
        std::uint16_t brokerPort{kDefaultMqttBrokerPort};
        std::string clientId{"industrial-hmi"};
        std::chrono::seconds keepAlive{kDefaultMqttKeepAlive};
        std::chrono::milliseconds heartbeatInterval{
            kDefaultMqttHeartbeatInterval};
    };

    explicit MqttClient(Config config);

    ~MqttClient() override;

    // IntegrationBackend
    void start() override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override {
        return running_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::string name() const override { return "MQTT"; }
    [[nodiscard]] BackendState connectionState() const noexcept override;
    [[nodiscard]] std::string metricsSummary() const override;

    // TelemetryPublisher
    void publish(const std::string& topic,
                 const std::string& payload) override;
    [[nodiscard]] bool canPublish() const override { return isRunning(); }

    // TelemetrySubscriber
    /// Register an exact-match topic filter + callback. The SUBSCRIBE
    /// packet flies to the broker on the next io_context tick (or on
    /// reconnect if start() hasn't completed yet -- queued
    /// subscriptions replay automatically). Callback is invoked on the
    /// io_context worker thread for every inbound PUBLISH whose topic
    /// equals the filter; callers must lock or marshal if they touch
    /// shared state.
    void subscribe(const std::string& topicFilter,
                   MessageCallback callback) override;

    /// How many PUBLISH frames have been written to the socket since
    /// start(). Useful for dashboards and integration tests that want
    /// to assert the publisher is actually flowing without parsing
    /// TCP traffic.
    [[nodiscard]] std::uint64_t publishedCount() const noexcept {
        return publishedCount_.load(std::memory_order_acquire);
    }

    /// How many inbound PUBLISH frames have been dispatched to
    /// subscriber callbacks since start(). Mirror of publishedCount()
    /// for the inbound direction; lets tests assert the read loop is
    /// alive without scraping a callback log.
    [[nodiscard]] std::uint64_t receivedCount() const noexcept {
        return receivedCount_.load(std::memory_order_acquire);
    }

private:
    /// Non-virtual stop body. Called from both the public virtual
    /// stop() and the destructor; the destructor cannot call a virtual
    /// safely (clang-analyzer-optin.cplusplus.VirtualCall).
    void stopImpl() noexcept;

    /// io_context worker thread body.
    void runIoLoop();

    /// Synchronously dial the broker, send CONNECT, await CONNACK.
    /// Throws on any failure (DNS, TCP refuse, broker rejection).
    void connectToBroker();

    /// Schedule the next PINGREQ via the steady_timer; chains itself.
    void scheduleHeartbeat();

    /// Replay every queued subscription onto the open socket. Called
    /// once at the end of start() (after CONNACK) and would also fire
    /// after a future reconnect path.
    void replaySubscriptions();

    /// Send a SUBSCRIBE packet for `topicFilter`. Must run on the io
    /// thread; subscribe() posts to it. No-op if the socket is down.
    void sendSubscribeFrame(const std::string& topicFilter);

    /// Kick off the persistent async read loop. After CONNACK we
    /// expect SUBACK / PUBLISH / PINGRESP frames at any time; this
    /// chain serialises them through `dispatchFrame`. The chain is
    /// split across three member functions so each step's completion
    /// handler captures only `this` (no fragile reference-to-lambda).
    void startReadLoop();
    /// Step 2/3 of the chain: read one more byte of the variable-
    /// length "remaining length" field, recurse if the continuation
    /// bit is still set, else move to readFrameBody.
    void readRemainingLengthByte();
    /// Step 3/3: pull `bodyLen` bytes off the wire, dispatch the
    /// complete frame, then loop back to step 1.
    void readFrameBody(std::uint32_t bodyLen);

    /// Dispatch a complete inbound frame to the appropriate handler.
    /// `fixedHeader` includes both the type nibble and the flags; the
    /// `body` excludes the fixed header + remaining-length prefix.
    void dispatchFrame(std::uint8_t fixedHeader,
                       const std::vector<std::uint8_t>& remainingLength,
                       const std::vector<std::uint8_t>& body);

    /// Deliver a parsed PUBLISH frame to any matching callbacks.
    /// Exact-string topic match (no wildcard resolution).
    void deliverInboundPublish(std::string_view topic,
                               std::string_view payload);

    Config config_;

    std::unique_ptr<boost::asio::io_context> io_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> publishedCount_{0};
    std::atomic<std::uint64_t> receivedCount_{0};
    std::atomic<std::uint16_t> nextPacketId_{1};

    /// Topic filter -> callback registry. Guarded so subscribe() can
    /// be invoked safely from arbitrary threads.
    mutable std::mutex subscriptionsMutex_;
    std::unordered_map<std::string, MessageCallback> subscriptions_;

    /// Opaque pimpl-style holder for the per-session TCP socket,
    /// heartbeat timer, and the read-loop scratch buffers. Defined in
    /// the .cpp so the header doesn't pull boost/asio.hpp into every
    /// TU that includes us.
    struct Session;
    std::unique_ptr<Session> session_;
};

}  // namespace app::integration
