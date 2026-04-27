// Tests for app::integration::MqttPublisher.
//
// Brings up an in-process MockMqttBroker (a tiny Asio TCP server)
// that:
//   1. Accepts the publisher's TCP connection.
//   2. Reads the CONNECT packet, replies with CONNACK (Accepted).
//   3. Records every subsequent PUBLISH frame for assertion.
//   4. Honours DISCONNECT and tears the connection down cleanly.
//
// All tests run on loopback. No external broker, no fixtures other
// than gtest/gmock.

#include "src/integration/MqttPublisher.h"

#include "src/integration/MqttPacket.h"

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using app::integration::MqttPublisher;
namespace mqtt = app::integration::mqtt;

namespace asio = boost::asio;
using boost::asio::ip::tcp;

namespace {

constexpr std::chrono::milliseconds kBrokerSettleDelay{50};
constexpr std::chrono::milliseconds kPublishSettleDelay{100};
constexpr std::chrono::milliseconds kHeartbeatInterval{200};
constexpr std::chrono::seconds kKeepAlive{30};

/// Captured PUBLISH frame -- topic + payload as the broker observed
/// them on the wire after MQTT framing.
struct PublishedFrame {
    std::string topic;
    std::string payload;
};

/// Read a 16-bit big-endian length prefix from a byte iterator.
std::uint16_t readUint16BigEndian(const std::vector<std::uint8_t>& buf,
                                  std::size_t offset) {
    constexpr int kHighShift = 8;
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(buf[offset]) << kHighShift) |
        static_cast<std::uint16_t>(buf[offset + 1]));
}

/// Tiny in-process MQTT broker. Just enough behaviour to let
/// MqttPublisher complete its CONNECT handshake and observe the
/// PUBLISH frames it emits afterwards.
class MockMqttBroker {
public:
    MockMqttBroker()
        : acceptor_(io_, tcp::endpoint(tcp::v4(), 0)) {
        port_ = acceptor_.local_endpoint().port();
        thread_ = std::jthread([this]() { runAcceptLoop(); });
    }

    ~MockMqttBroker() {
        stop();
    }

    void stop() {
        if (stopped_.exchange(true)) return;
        boost::system::error_code ec;
        acceptor_.close(ec);
        io_.stop();
        if (thread_.joinable()) thread_.join();
    }

    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    [[nodiscard]] std::vector<PublishedFrame> publishedFrames() const {
        const std::scoped_lock lock(mutex_);
        return frames_;
    }

    [[nodiscard]] bool sawConnect() const noexcept {
        return sawConnect_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool sawDisconnect() const noexcept {
        return sawDisconnect_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::size_t pingReqCount() const noexcept {
        return pingReqCount_.load(std::memory_order_acquire);
    }

private:
    void runAcceptLoop() {
        try {
            tcp::socket socket(io_);
            boost::system::error_code ec;
            acceptor_.accept(socket, ec);
            if (ec) return;
            handleClient(socket);
        } catch (...) {
            // Acceptor was closed during stop() -- exit cleanly.
        }
    }

    void handleClient(tcp::socket& socket) {
        // 1. Read CONNECT.
        try {
            const auto connectBytes = readFullPacket(socket);
            if (connectBytes.empty()) return;
            if ((connectBytes[0] & 0xF0) ==
                static_cast<std::uint8_t>(mqtt::PacketType::Connect)) {
                sawConnect_.store(true, std::memory_order_release);
            }

            // 2. Reply with CONNACK Accepted.
            const std::vector<std::uint8_t> connack{
                static_cast<std::uint8_t>(mqtt::PacketType::ConnAck),
                0x02, 0x00, 0x00};
            asio::write(socket, asio::buffer(connack));

            // 3. Loop reading subsequent packets (PUBLISH / PINGREQ /
            //    DISCONNECT) until the client closes or disconnects.
            while (!stopped_.load(std::memory_order_acquire)) {
                const auto bytes = readFullPacket(socket);
                if (bytes.empty()) break;
                handleClientPacket(bytes);
                if (sawDisconnect_.load(std::memory_order_acquire)) break;
            }
        } catch (...) {
            // Any I/O error -- client likely went away.
        }
        boost::system::error_code ec;
        socket.close(ec);
    }

    void handleClientPacket(const std::vector<std::uint8_t>& bytes) {
        if (bytes.empty()) return;
        const auto type = static_cast<std::uint8_t>(bytes[0] & 0xF0);

        if (type == static_cast<std::uint8_t>(mqtt::PacketType::Publish)) {
            // Parse topic + payload. Variable header: 2-byte topic
            // length, then topic. Remaining bytes after topic = payload
            // (QoS 0, no packet id).
            std::size_t cursor = 1;
            (void)mqtt::decodeRemainingLength(bytes, cursor);
            const auto topicLen = readUint16BigEndian(bytes, cursor);
            cursor += sizeof(std::uint16_t);
            std::string topic(bytes.begin() + cursor,
                              bytes.begin() + cursor + topicLen);
            cursor += topicLen;
            std::string payload(bytes.begin() + cursor, bytes.end());

            const std::scoped_lock lock(mutex_);
            frames_.push_back({std::move(topic), std::move(payload)});
        } else if (type == static_cast<std::uint8_t>(
                              mqtt::PacketType::PingReq)) {
            pingReqCount_.fetch_add(1, std::memory_order_release);
        } else if (type == static_cast<std::uint8_t>(
                              mqtt::PacketType::Disconnect)) {
            sawDisconnect_.store(true, std::memory_order_release);
        }
    }

    /// Read one MQTT control packet (fixed header + variable-length
    /// remaining length + payload). Returns empty vector on EOF.
    std::vector<std::uint8_t> readFullPacket(tcp::socket& socket) {
        // Read first byte (fixed header type).
        std::uint8_t firstByte = 0;
        boost::system::error_code ec;
        const std::size_t n =
            asio::read(socket, asio::buffer(&firstByte, 1), ec);
        if (ec || n == 0) return {};

        // Read remaining-length variable-length encoding (1..4 bytes).
        std::vector<std::uint8_t> remainingLengthBytes;
        for (int i = 0; i < 4; ++i) {
            std::uint8_t byte = 0;
            asio::read(socket, asio::buffer(&byte, 1), ec);
            if (ec) return {};
            remainingLengthBytes.push_back(byte);
            if ((byte & 0x80U) == 0) break;
        }
        std::size_t cursor = 0;
        const auto remaining =
            mqtt::decodeRemainingLength(remainingLengthBytes, cursor);

        // Read body.
        std::vector<std::uint8_t> body(remaining);
        if (remaining > 0) {
            asio::read(socket, asio::buffer(body), ec);
            if (ec) return {};
        }

        // Reassemble the full packet so handleClientPacket can use
        // the same offsets the publisher used to write it.
        std::vector<std::uint8_t> packet;
        packet.push_back(firstByte);
        packet.insert(packet.end(), remainingLengthBytes.begin(),
                      remainingLengthBytes.begin() + cursor);
        packet.insert(packet.end(), body.begin(), body.end());
        return packet;
    }

    asio::io_context io_;
    tcp::acceptor acceptor_;
    std::jthread thread_;
    std::uint16_t port_{0};

    mutable std::mutex mutex_;
    std::vector<PublishedFrame> frames_;
    std::atomic<bool> sawConnect_{false};
    std::atomic<bool> sawDisconnect_{false};
    std::atomic<std::size_t> pingReqCount_{0};
    std::atomic<bool> stopped_{false};
};

MqttPublisher::Config makeConfig(std::uint16_t port) {
    MqttPublisher::Config c;
    c.brokerHost = "127.0.0.1";
    c.brokerPort = port;
    c.clientId = "test-client";
    c.keepAlive = kKeepAlive;
    c.heartbeatInterval = kHeartbeatInterval;
    return c;
}

}  // namespace

// Lifecycle

TEST(MqttPublisherTest, ConnectsAndCompletesCONNECTHandshake) {
    MockMqttBroker broker;
    MqttPublisher pub(makeConfig(broker.port()));

    pub.start();
    EXPECT_TRUE(pub.isRunning());
    EXPECT_TRUE(pub.canPublish());

    std::this_thread::sleep_for(kBrokerSettleDelay);
    EXPECT_TRUE(broker.sawConnect());

    pub.stop();
    EXPECT_FALSE(pub.isRunning());
}

TEST(MqttPublisherTest, RaisesOnConnectionRefused) {
    // Bind to a dead port (use a port that should not have a listener).
    // We grab an OS-assigned port via the broker helper, then stop it
    // immediately so the publisher's connect attempt sees ECONNREFUSED.
    std::uint16_t deadPort = 0;
    {
        MockMqttBroker broker;
        deadPort = broker.port();
    }
    MqttPublisher pub(makeConfig(deadPort));
    EXPECT_THROW(pub.start(), std::exception);
    EXPECT_FALSE(pub.isRunning());
}

TEST(MqttPublisherTest, NameIsMqtt) {
    MqttPublisher pub(makeConfig(0));
    EXPECT_EQ(pub.name(), "MQTT");
}

TEST(MqttPublisherTest, StartIsIdempotent) {
    MockMqttBroker broker;
    MqttPublisher pub(makeConfig(broker.port()));
    pub.start();
    EXPECT_NO_THROW(pub.start());  // second call is a no-op
    pub.stop();
}

TEST(MqttPublisherTest, StopIsIdempotent) {
    MockMqttBroker broker;
    MqttPublisher pub(makeConfig(broker.port()));
    pub.start();
    pub.stop();
    EXPECT_NO_THROW(pub.stop());
}

// Publish path

TEST(MqttPublisherTest, PublishEmitsPublishFrame) {
    MockMqttBroker broker;
    MqttPublisher pub(makeConfig(broker.port()));
    pub.start();
    std::this_thread::sleep_for(kBrokerSettleDelay);

    pub.publish("topic/a", "payload-1");
    std::this_thread::sleep_for(kPublishSettleDelay);

    const auto frames = broker.publishedFrames();
    ASSERT_FALSE(frames.empty());
    EXPECT_EQ(frames[0].topic,   "topic/a");
    EXPECT_EQ(frames[0].payload, "payload-1");
    EXPECT_GT(pub.publishedCount(), 0U);

    pub.stop();
}

TEST(MqttPublisherTest, PublishMultipleFramesPreservesOrder) {
    MockMqttBroker broker;
    MqttPublisher pub(makeConfig(broker.port()));
    pub.start();
    std::this_thread::sleep_for(kBrokerSettleDelay);

    pub.publish("topic/x", "first");
    pub.publish("topic/y", "second");
    pub.publish("topic/z", "third");
    std::this_thread::sleep_for(kPublishSettleDelay);

    const auto frames = broker.publishedFrames();
    ASSERT_GE(frames.size(), 3U);
    EXPECT_EQ(frames[0].payload, "first");
    EXPECT_EQ(frames[1].payload, "second");
    EXPECT_EQ(frames[2].payload, "third");

    pub.stop();
}

TEST(MqttPublisherTest, PublishWhileNotRunningIsNoOp) {
    MqttPublisher pub(makeConfig(0));
    EXPECT_NO_THROW(pub.publish("t", "p"));
    EXPECT_EQ(pub.publishedCount(), 0U);
    EXPECT_FALSE(pub.canPublish());
}

// Heartbeat

TEST(MqttPublisherTest, EmitsPingReqOnHeartbeatInterval) {
    MockMqttBroker broker;
    MqttPublisher pub(makeConfig(broker.port()));
    pub.start();

    // Wait for at least 2 heartbeat intervals.
    std::this_thread::sleep_for(kHeartbeatInterval * 3);

    EXPECT_GE(broker.pingReqCount(), 1U)
        << "heartbeat timer should have fired at least once";

    pub.stop();
}

// Clean disconnect

TEST(MqttPublisherTest, StopSendsDisconnectFrame) {
    MockMqttBroker broker;
    MqttPublisher pub(makeConfig(broker.port()));
    pub.start();
    std::this_thread::sleep_for(kBrokerSettleDelay);

    pub.stop();
    std::this_thread::sleep_for(kBrokerSettleDelay);

    EXPECT_TRUE(broker.sawDisconnect())
        << "stop() should send a clean DISCONNECT before closing";
}
