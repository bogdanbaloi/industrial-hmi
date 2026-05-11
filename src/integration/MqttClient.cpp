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

/// Bit mask extracting the packet type nibble from an MQTT fixed
/// header byte (the low nibble carries DUP/QoS/RETAIN flags).
constexpr std::uint8_t kFixedHeaderTypeMask = 0xF0U;

/// MQTT remaining length is variable-length encoded over up to 4
/// bytes (spec 2.2.3). The continuation bit is the high bit; once
/// it's clear the length field is complete.
constexpr std::uint8_t kRemainingLengthContinuationBit = 0x80U;
constexpr std::size_t  kMaxRemainingLengthBytes        = 4U;

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

    /// Scratch buffers for the async read state machine. They live
    /// per-session (not per-frame) so the async chain can refer to
    /// stable memory addresses across handlers without heap-allocating
    /// per packet.
    std::uint8_t              readFixedHeader{0};
    std::uint8_t              readLengthByte{0};
    std::vector<std::uint8_t> readRemainingLengthBytes;
    std::vector<std::uint8_t> readBody;
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
    startReadLoop();
    replaySubscriptions();
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

void MqttClient::subscribe(const std::string& topicFilter,
                           MessageCallback callback) {
    // Topic filter validation: empty filters are nonsensical and
    // brokers reject them. Length cap mirrors what encodeString /
    // buildSubscribe accept.
    if (topicFilter.empty() || topicFilter.size() > mqtt::kMaxStringLength) {
        return;
    }

    // Register the callback immediately so a SUBSCRIBE that fires
    // before the corresponding SUBACK still routes inbound frames.
    {
        const std::lock_guard<std::mutex> lock(subscriptionsMutex_);
        subscriptions_.insert_or_assign(topicFilter, std::move(callback));
    }

    // If we're not running yet, the call landed pre-start(); the
    // SUBSCRIBE will be replayed by replaySubscriptions() once we're
    // connected. Otherwise, fire it now on the io thread.
    if (!running_.load(std::memory_order_acquire) || !session_) return;
    asio::post(*io_, [this, topicFilter]() {
        sendSubscribeFrame(topicFilter);
    });
}

void MqttClient::replaySubscriptions() {
    // Snapshot the keyset so we don't hold the lock while doing socket
    // I/O. The map itself can't shrink concurrently with start() (no
    // subscribe() is mid-flight from another thread while we're still
    // inside start()), but holding a mutex over a blocking write is
    // bad form regardless.
    std::vector<std::string> topics;
    {
        const std::lock_guard<std::mutex> lock(subscriptionsMutex_);
        topics.reserve(subscriptions_.size());
        for (const auto& [filter, _] : subscriptions_) topics.push_back(filter);
    }
    for (const auto& topic : topics) sendSubscribeFrame(topic);
}

void MqttClient::sendSubscribeFrame(const std::string& topicFilter) {
    if (!session_ || !running_.load(std::memory_order_acquire)) return;
    const auto packetId =
        nextPacketId_.fetch_add(1, std::memory_order_relaxed);
    const auto bytes = mqtt::buildSubscribe(packetId, topicFilter);
    boost::system::error_code ec;
    (void)asio::write(session_->socket, asio::buffer(bytes), ec);
    if (ec) {
        // Socket dropped mid-subscribe -- mark down so subsequent
        // publish() / heartbeat() short-circuits cleanly.
        running_.store(false, std::memory_order_release);
    }
}

// The three read-loop steps form a cyclic chain via boost::asio
// `async_read` completion handlers (startReadLoop -> read length byte
// -> read body -> startReadLoop again). clang-tidy
// `misc-no-recursion` flags the cycle because the boost templates make
// it look like recursion, but in practice the io_context unwinds the
// stack between handlers. Suppress on the three step entry points.
// NOLINTNEXTLINE(misc-no-recursion)
void MqttClient::startReadLoop() {
    if (!session_) return;
    session_->readRemainingLengthBytes.clear();
    session_->readBody.clear();

    // Step 1: read the single fixed-header byte. The remaining-length
    // field that follows is variable; readRemainingLengthByte() handles
    // it byte by byte.
    asio::async_read(
        session_->socket, asio::buffer(&session_->readFixedHeader, 1),
        // NOLINTNEXTLINE(misc-no-recursion)
        [this](const boost::system::error_code& ec, std::size_t /*n*/) {
            if (ec) {
                // operation_aborted = stop() cancelled us, normal path.
                // Anything else means the broker dropped us mid-stream.
                if (ec != asio::error::operation_aborted) {
                    running_.store(false, std::memory_order_release);
                }
                return;
            }
            if (!session_) return;
            readRemainingLengthByte();
        });
}

// NOLINTNEXTLINE(misc-no-recursion)
void MqttClient::readRemainingLengthByte() {
    if (!session_) return;
    asio::async_read(
        session_->socket, asio::buffer(&session_->readLengthByte, 1),
        // NOLINTNEXTLINE(misc-no-recursion)
        [this](const boost::system::error_code& ec, std::size_t /*n*/) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    running_.store(false, std::memory_order_release);
                }
                return;
            }
            if (!session_) return;

            session_->readRemainingLengthBytes.push_back(
                session_->readLengthByte);
            const bool more =
                (session_->readLengthByte &
                 kRemainingLengthContinuationBit) != 0;

            if (more) {
                if (session_->readRemainingLengthBytes.size() >=
                        kMaxRemainingLengthBytes) {
                    // Continuation bit set after 4 bytes -- malformed
                    // remaining-length field per spec 2.2.3.
                    running_.store(false, std::memory_order_release);
                    return;
                }
                readRemainingLengthByte();
                return;
            }

            // Length field complete; decode and pull the body.
            std::size_t cursor = 0;
            std::uint32_t bodyLen = 0;
            try {
                bodyLen = mqtt::decodeRemainingLength(
                    session_->readRemainingLengthBytes, cursor);
            } catch (...) {
                running_.store(false, std::memory_order_release);
                return;
            }
            readFrameBody(bodyLen);
        });
}

// NOLINTNEXTLINE(misc-no-recursion)
void MqttClient::readFrameBody(std::uint32_t bodyLen) {
    if (!session_) return;
    session_->readBody.resize(bodyLen);
    asio::async_read(
        session_->socket, asio::buffer(session_->readBody),
        // NOLINTNEXTLINE(misc-no-recursion)
        [this](const boost::system::error_code& ec, std::size_t /*n*/) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    running_.store(false, std::memory_order_release);
                }
                return;
            }
            if (!session_) return;
            dispatchFrame(session_->readFixedHeader,
                          session_->readRemainingLengthBytes,
                          session_->readBody);
            // Loop: read the next frame.
            startReadLoop();
        });
}

void MqttClient::dispatchFrame(
    std::uint8_t fixedHeader,
    const std::vector<std::uint8_t>& remainingLength,
    const std::vector<std::uint8_t>& body) {
    using mqtt::PacketType;
    const auto type =
        static_cast<PacketType>(fixedHeader & kFixedHeaderTypeMask);

    switch (type) {
    case PacketType::Publish: {
        // Reassemble the full packet for parsePublish (which expects
        // the fixed header byte + remaining-length prefix + body).
        std::vector<std::uint8_t> packet;
        packet.reserve(1 + remainingLength.size() + body.size());
        packet.push_back(fixedHeader);
        packet.insert(packet.end(),
                      remainingLength.begin(), remainingLength.end());
        packet.insert(packet.end(), body.begin(), body.end());
        try {
            const auto parsed = mqtt::parsePublish(packet);
            deliverInboundPublish(parsed.topic, parsed.payload);
            receivedCount_.fetch_add(1, std::memory_order_release);
        } catch (...) {  // NOLINT(bugprone-empty-catch)
            // Malformed PUBLISH (QoS > 0, truncated, etc.). Skip but
            // keep the connection alive; the broker may be sending us
            // frames we don't yet support. No logger available this
            // deep in the io thread and there is nowhere meaningful
            // to surface a single bad frame.
        }
        break;
    }
    // Acknowledgements + keep-alive replies all share "no state to
    // update beyond the connection being alive". Default is the same
    // shape today (silent drop) but is semantically a different case
    // -- a protocol-violation from the broker -- which would grow a
    // logger call here once we wire one in.
    // NOLINTNEXTLINE(bugprone-branch-clone)
    case PacketType::SubAck:
    case PacketType::UnsubAck:
    case PacketType::PingResp:
        break;
    default:
        // Unexpected packet types from a broker (CONNECT etc.) are
        // protocol violations. Quietly ignore to keep the loop alive.
        break;
    }
}

void MqttClient::deliverInboundPublish(std::string_view topic,
                                       std::string_view payload) {
    MessageCallback cb;
    {
        const std::lock_guard<std::mutex> lock(subscriptionsMutex_);
        const auto it = subscriptions_.find(std::string{topic});
        if (it == subscriptions_.end()) return;
        cb = it->second;
    }
    // Invoke outside the lock so a callback that re-enters subscribe()
    // doesn't deadlock.
    if (cb) cb(topic, payload);
}

}  // namespace app::integration
