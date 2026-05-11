#include "src/integration/MqttClient.h"

#include "src/integration/MqttPacket.h"

#include <boost/asio.hpp>

#include <chrono>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace app::integration {

namespace asio = boost::asio;
using boost::asio::ip::tcp;

namespace {

// Empirically-safe upper bound on CONNACK arrival. The broker should
// reply within a few RTTs; if it doesn't, something is wrong (firewall,
// auth provider, etc.) and start() raises rather than hanging.
constexpr std::chrono::seconds kConnAckTimeout{10};

}  // namespace

// Pimpl holder so the header stays Asio-free.
//
// Holds the per-session TCP socket, heartbeat timer, AND a
// work_guard. The work_guard pins the io_context's run loop alive
// across the construction-then-thread-start window: without it,
// connect-time `io_context::run_one()` calls (used to drain the
// CONNACK deadline timer) leave the run queue empty, which makes
// the worker thread's later `io_context::run()` exit immediately
// and silently drop every queued publish() and the heartbeat timer.
struct MqttClient::Session {
    explicit Session(asio::io_context& io)
        : socket(io),
          heartbeatTimer(io),
          workGuard(asio::make_work_guard(io)) {}

    tcp::socket socket;
    asio::steady_timer heartbeatTimer;
    asio::executor_work_guard<asio::io_context::executor_type> workGuard;
};

MqttClient::MqttClient(Config config)
    : config_(std::move(config)),
      io_(std::make_unique<asio::io_context>()) {}

MqttClient::~MqttClient() {
    stopImpl();
}

void MqttClient::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;  // already running, idempotent
    }

    session_ = std::make_unique<Session>(*io_);

    try {
        connectToBroker();
    } catch (...) {
        // CONNECT failed: roll back state so isRunning() reflects reality
        // and start() can be retried once the broker comes back.
        session_.reset();
        running_.store(false, std::memory_order_release);
        throw;
    }

    scheduleHeartbeat();
    thread_ = std::jthread([this]() { runIoLoop(); });
}

void MqttClient::stop() {
    stopImpl();
}

void MqttClient::stopImpl() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    // Best-effort DISCONNECT so the broker logs a clean disconnect
    // rather than a stale-connection timeout. We skip the explicit
    // socket.close() because session_.reset() below destructs the
    // socket, which closes the fd via RAII -- avoids the bugprone-
    // unused-return-value clang-tidy hit on close(ec).
    if (session_) {
        boost::system::error_code ignoredEc;
        const auto bytes = mqtt::buildDisconnect();
        (void)asio::write(session_->socket, asio::buffer(bytes), ignoredEc);

        // Post timer cancel + work_guard release on the io_context's
        // executor so they serialise with the heartbeat callback that
        // runs on the worker thread (the callback calls
        // timer.expires_after() which races with timer.cancel() if both
        // happen on different threads -- boost::asio::basic_waitable_timer
        // is not thread-safe across these calls; TSan caught it).
        // Releasing the work_guard from inside the executor unblocks
        // run() the same way the previous direct reset did.
        asio::post(*io_, [this]() {
            session_->heartbeatTimer.cancel();
            session_->workGuard.reset();
        });
    }

    // Stop the io_context, join the worker, then reset for a future start().
    // Shutdown is noexcept by contract; logger isn't available this late
    // and there's nowhere meaningful to surface a stop-time failure.
    try {
        if (io_) io_->stop();
        if (thread_.joinable()) thread_.join();
        session_.reset();
        io_ = std::make_unique<asio::io_context>();
        // NOLINTNEXTLINE(bugprone-empty-catch)
    } catch (...) { /* swallow */ }
}

void MqttClient::runIoLoop() {
    try {
        io_->run();
    } catch (...) {
        running_.store(false, std::memory_order_release);
    }
}

BackendState MqttClient::connectionState() const noexcept {
    // The publisher reaches `Connected` only after CONNECT/CONNACK
    // succeeded -- the worker thread is alive AND the session is
    // armed. start() either completes the handshake synchronously
    // (running_ flips true after CONNACK) or throws and rolls back to
    // running_ == false, so there's no observable "Connecting" window
    // here. If the heartbeat callback later flips running_ off after a
    // socket write failure, we surface that as Degraded so operators
    // see a yellow dot instead of an indistinguishable Disconnected
    // baseline.
    if (running_.load(std::memory_order_acquire)) {
        return BackendState::Connected;
    }
    // The worker thread sets running_ = false on io_context::run()
    // exception (broker dropped, socket reset). publishedCount_ being
    // non-zero proves we DID reach Connected at some point in this
    // process lifetime; a transition to running_=false from there is
    // a degradation, not a clean shutdown.
    if (publishedCount_.load(std::memory_order_acquire) > 0) {
        return BackendState::Degraded;
    }
    return BackendState::Disconnected;
}

std::string MqttClient::metricsSummary() const {
    return std::format("broker {}:{} | {} publishes",
                       config_.brokerHost,
                       config_.brokerPort,
                       publishedCount_.load(std::memory_order_acquire));
}

void MqttClient::connectToBroker() {
    auto& socket = session_->socket;

    // Synchronous resolve + connect. Asio raises an exception (caught
    // upstream) on resolve failure / ECONNREFUSED.
    tcp::resolver resolver(*io_);
    const auto endpoints = resolver.resolve(
        config_.brokerHost, std::to_string(config_.brokerPort));
    asio::connect(socket, endpoints);

    // Send CONNECT, then read CONNACK with a deadline so we don't hang
    // forever on a broker that accepts the TCP handshake but never
    // replies. The Session's work_guard keeps the io_context alive
    // across this `run_one()` so the post-connect heartbeat timer +
    // publish() posts are still serviced by the worker thread.
    const auto connectBytes = mqtt::buildConnect(config_.clientId,
                                                  config_.keepAlive);
    asio::write(socket, asio::buffer(connectBytes));

    asio::steady_timer deadline(*io_);
    deadline.expires_after(kConnAckTimeout);
    bool deadlineFired = false;
    deadline.async_wait([&](const boost::system::error_code& ec) {
        if (!ec) {
            deadlineFired = true;
            boost::system::error_code ignoredEc;
            // cancel() return is intentionally discarded -- failures
            // here are silent and the deadlineFired flag drives the
            // post-read error path. clang-tidy's bugprone-unused-
            // return-value still flags `(void)` casts on this overload,
            // so we use NOLINT directly on the call.
            // NOLINTNEXTLINE(bugprone-unused-return-value)
            socket.cancel(ignoredEc);
        }
    });

    std::vector<std::uint8_t> buf(mqtt::kConnAckPacketSize);
    boost::system::error_code readEc;
    asio::read(socket, asio::buffer(buf), readEc);
    deadline.cancel();
    io_->run_one();  // drain the deadline cancellation handler

    if (deadlineFired) {
        throw std::runtime_error("MQTT broker did not send CONNACK in time");
    }
    if (readEc) {
        throw std::runtime_error(
            std::format("MQTT CONNACK read failed: {}", readEc.message()));
    }

    const auto code = mqtt::parseConnAck(buf);
    if (code != mqtt::ConnAckCode::Accepted) {
        throw std::runtime_error(std::format(
            "MQTT broker rejected CONNECT: code 0x{:02x}",
            static_cast<unsigned>(code)));
    }
}

void MqttClient::scheduleHeartbeat() {
    if (!session_ || !running_.load(std::memory_order_acquire)) return;

    session_->heartbeatTimer.expires_after(config_.heartbeatInterval);
    session_->heartbeatTimer.async_wait(
        [this](const boost::system::error_code& ec) {
            if (ec) return;  // cancelled by stop()
            if (!session_) return;

            boost::system::error_code writeEc;
            const auto bytes = mqtt::buildPingReq();
            (void)asio::write(session_->socket, asio::buffer(bytes), writeEc);
            if (writeEc) {
                running_.store(false, std::memory_order_release);
                return;
            }

            scheduleHeartbeat();
        });
}

void MqttClient::publish(const std::string& topic,
                            const std::string& payload) {
    if (!canPublish() || !session_) return;

    // Marshal to the io_context worker so the socket is only touched
    // by one thread regardless of where the caller fired.
    asio::post(*io_, [this, topic, payload]() {
        if (!session_) return;
        const auto bytes = mqtt::buildPublish(topic, payload);
        boost::system::error_code ec;
        (void)asio::write(session_->socket, asio::buffer(bytes), ec);
        if (ec) {
            // Connection died mid-session. Mark the backend down so
            // canPublish() reflects reality; future writes short-circuit.
            running_.store(false, std::memory_order_release);
            return;
        }
        publishedCount_.fetch_add(1, std::memory_order_release);
    });
}

}  // namespace app::integration
