#pragma once

#include "src/integration/IntegrationBackend.h"
#include "src/integration/TelemetryPublisher.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

// Forward-declare Asio types so the header doesn't pull <boost/asio.hpp>
// into every translation unit that includes us. Foreign API naming --
// we don't get to rename Boost's io_context to CamelCase.
namespace boost::asio {
    // NOLINTNEXTLINE(readability-identifier-naming)
    class io_context;
}

namespace app::integration {

/// MQTT 3.1.1 publisher backend (domain-agnostic).
///
/// Connects to a remote broker over plain TCP and exposes a generic
/// `publish(topic, payload)` surface via the TelemetryPublisher
/// interface. It does NOT subscribe to any specific domain model --
/// callers (e.g. ProductionTelemetryBridge) own the topic vocabulary
/// and call publish() with their own data.
///
/// This separation means the same MqttPublisher instance can serve
/// multiple verticals: a manufacturing deployment wires it to a
/// ProductionTelemetryBridge; a smart-building deployment wires it
/// to a HvacTelemetryBridge; etc. The MQTT-side code stays identical.
///
/// Wire format implemented by hand in MqttPacket.h (~200 lines, no
/// system library dependency). MQTT 3.1.1 + QoS 0 PUBLISH only.
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

class MqttPublisher : public IntegrationBackend, public TelemetryPublisher {
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

    explicit MqttPublisher(Config config);

    ~MqttPublisher() override;

    // IntegrationBackend
    void start() override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override {
        return running_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::string name() const override { return "MQTT"; }

    // TelemetryPublisher
    void publish(const std::string& topic,
                 const std::string& payload) override;
    [[nodiscard]] bool canPublish() const override { return isRunning(); }

    /// How many PUBLISH frames have been written to the socket since
    /// start(). Useful for dashboards and integration tests that want
    /// to assert the publisher is actually flowing without parsing
    /// TCP traffic.
    [[nodiscard]] std::uint64_t publishedCount() const noexcept {
        return publishedCount_.load(std::memory_order_acquire);
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

    Config config_;

    std::unique_ptr<boost::asio::io_context> io_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> publishedCount_{0};

    /// Opaque pimpl-style holder for the per-session TCP socket and
    /// heartbeat timer. Defined in the .cpp so the header doesn't
    /// pull boost/asio.hpp into every TU that includes it.
    struct Session;
    std::unique_ptr<Session> session_;
};

}  // namespace app::integration
