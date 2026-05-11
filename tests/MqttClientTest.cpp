// Tests for app::integration::MqttClient.
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

#include "src/integration/MqttClient.h"

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

using app::integration::MqttClient;
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
/// MqttClient complete its CONNECT handshake and observe the
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

    // Synchronous accept on the worker thread is paired with a
    // synchronous close on main -- but boost::asio::basic_socket_acceptor
    // is NOT thread-safe across accept/close, so calling close() while
    // accept() is in flight races on the acceptor's implementation
    // state (TSan caught this). The fix: self-connect to the listening
    // port, which unblocks accept(); the worker observes stopped_ ==
    // true and returns; only after thread_.join() (no concurrent
    // access) do we close the acceptor on the main thread.
    void stop() {
        if (stopped_.exchange(true)) return;
        if (thread_.joinable()) {
            boost::asio::io_context wakerIo;
            tcp::socket waker(wakerIo);
            boost::system::error_code ec;
            waker.connect(
                tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"),
                              port_),
                ec);
            // Ignore connect failure -- if the worker already exited
            // (acceptor died, port closed) the join below still works.
            thread_.join();
        }
        boost::system::error_code ec;
        acceptor_.close(ec);
        io_.stop();
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
    [[nodiscard]] bool sawSubscribe() const noexcept {
        return sawSubscribe_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::string lastSubscribedTopic() const {
        const std::scoped_lock lock(mutex_);
        return lastSubscribedTopic_;
    }

    /// On the next inbound SUBSCRIBE, the broker queues a PUBLISH
    /// echo of (`topic`, `payload`) back to the client. Lets tests
    /// drive the subscriber read path without spinning a second
    /// real MQTT client.
    void echoOnNextSubscribe(std::string topic, std::string payload) {
        const std::scoped_lock lock(mutex_);
        echoTopic_   = std::move(topic);
        echoPayload_ = std::move(payload);
        echoArmed_   = true;
    }

private:
    void runAcceptLoop() {
        try {
            tcp::socket socket(io_);
            boost::system::error_code ec;
            acceptor_.accept(socket, ec);
            // stop() may have woken us via self-connect; the dummy
            // socket then disconnects without sending CONNECT. Bail
            // before handleClient blocks on a phantom protocol read.
            if (ec || stopped_.load(std::memory_order_acquire)) return;
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
                handleClientPacket(bytes, socket);
                if (sawDisconnect_.load(std::memory_order_acquire)) break;
            }
        } catch (...) {
            // Any I/O error -- client likely went away.
        }
        boost::system::error_code ec;
        socket.close(ec);
    }

    void handleClientPacket(const std::vector<std::uint8_t>& bytes,
                            tcp::socket& socket) {
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
                              mqtt::PacketType::Subscribe)) {
            handleSubscribe(bytes, socket);
        } else if (type == static_cast<std::uint8_t>(
                              mqtt::PacketType::Disconnect)) {
            sawDisconnect_.store(true, std::memory_order_release);
        }
    }

    /// Parse the topic filter out of a SUBSCRIBE, reply with SUBACK,
    /// and -- if a previous `echoOnNextSubscribe(...)` call armed an
    /// echo -- push a PUBLISH down the wire so the test can verify
    /// the client's read loop dispatches it.
    void handleSubscribe(const std::vector<std::uint8_t>& bytes,
                         tcp::socket& socket) {
        // SUBSCRIBE wire format (spec 3.8):
        //   fixed header (1) + remaining length (1..4) +
        //   variable header: packet identifier (2) +
        //   payload: topic filter (length-prefixed string) + QoS byte.
        std::size_t cursor = 1;
        (void)mqtt::decodeRemainingLength(bytes, cursor);
        const auto packetId = readUint16BigEndian(bytes, cursor);
        cursor += sizeof(std::uint16_t);
        const auto topicLen = readUint16BigEndian(bytes, cursor);
        cursor += sizeof(std::uint16_t);
        std::string topic(bytes.begin() + cursor,
                          bytes.begin() + cursor + topicLen);

        sawSubscribe_.store(true, std::memory_order_release);
        std::string echoTopic;
        std::string echoPayload;
        bool armed = false;
        {
            const std::scoped_lock lock(mutex_);
            lastSubscribedTopic_ = topic;
            if (echoArmed_) {
                echoTopic   = echoTopic_;
                echoPayload = echoPayload_;
                armed       = true;
                echoArmed_  = false;
            }
        }

        // Send SUBACK granting QoS 0 (spec 3.9).
        const std::vector<std::uint8_t> suback{
            static_cast<std::uint8_t>(mqtt::PacketType::SubAck),
            0x03,
            static_cast<std::uint8_t>((packetId >> 8U) & 0xFFU),
            static_cast<std::uint8_t>(packetId & 0xFFU),
            0x00  // GrantedQos0
        };
        boost::system::error_code ec;
        asio::write(socket, asio::buffer(suback), ec);

        if (armed && !ec) {
            // Push a PUBLISH frame back down the client's socket so
            // its read loop has something to dispatch. Build via the
            // same encoder the production code uses to guarantee wire
            // compatibility.
            const auto publish = mqtt::buildPublish(echoTopic, echoPayload);
            asio::write(socket, asio::buffer(publish), ec);
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
    std::atomic<bool> sawSubscribe_{false};
    std::atomic<std::size_t> pingReqCount_{0};
    std::atomic<bool> stopped_{false};
    std::string lastSubscribedTopic_;
    std::string echoTopic_;
    std::string echoPayload_;
    bool echoArmed_{false};
};

MqttClient::Config makeConfig(std::uint16_t port) {
    MqttClient::Config c;
    c.brokerHost = "127.0.0.1";
    c.brokerPort = port;
    c.clientId = "test-client";
    c.keepAlive = kKeepAlive;
    c.heartbeatInterval = kHeartbeatInterval;
    return c;
}

}  // namespace

// Lifecycle

TEST(MqttClientTest, ConnectsAndCompletesCONNECTHandshake) {
    MockMqttBroker broker;
    MqttClient pub(makeConfig(broker.port()));

    pub.start();
    EXPECT_TRUE(pub.isRunning());
    EXPECT_TRUE(pub.canPublish());

    std::this_thread::sleep_for(kBrokerSettleDelay);
    EXPECT_TRUE(broker.sawConnect());

    pub.stop();
    EXPECT_FALSE(pub.isRunning());
}

TEST(MqttClientTest, RaisesOnConnectionRefused) {
    // Bind to a dead port (use a port that should not have a listener).
    // We grab an OS-assigned port via the broker helper, then stop it
    // immediately so the publisher's connect attempt sees ECONNREFUSED.
    std::uint16_t deadPort = 0;
    {
        MockMqttBroker broker;
        deadPort = broker.port();
    }
    MqttClient pub(makeConfig(deadPort));
    EXPECT_THROW(pub.start(), std::exception);
    EXPECT_FALSE(pub.isRunning());
}

TEST(MqttClientTest, NameIsMqtt) {
    MqttClient pub(makeConfig(0));
    EXPECT_EQ(pub.name(), "MQTT");
}

TEST(MqttClientTest, StartIsIdempotent) {
    MockMqttBroker broker;
    MqttClient pub(makeConfig(broker.port()));
    pub.start();
    EXPECT_NO_THROW(pub.start());  // second call is a no-op
    pub.stop();
}

TEST(MqttClientTest, StopIsIdempotent) {
    MockMqttBroker broker;
    MqttClient pub(makeConfig(broker.port()));
    pub.start();
    pub.stop();
    EXPECT_NO_THROW(pub.stop());
}

// Publish path

TEST(MqttClientTest, PublishEmitsPublishFrame) {
    MockMqttBroker broker;
    MqttClient pub(makeConfig(broker.port()));
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

TEST(MqttClientTest, PublishMultipleFramesPreservesOrder) {
    MockMqttBroker broker;
    MqttClient pub(makeConfig(broker.port()));
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

TEST(MqttClientTest, PublishWhileNotRunningIsNoOp) {
    MqttClient pub(makeConfig(0));
    EXPECT_NO_THROW(pub.publish("t", "p"));
    EXPECT_EQ(pub.publishedCount(), 0U);
    EXPECT_FALSE(pub.canPublish());
}

// Heartbeat

TEST(MqttClientTest, EmitsPingReqOnHeartbeatInterval) {
    MockMqttBroker broker;
    MqttClient pub(makeConfig(broker.port()));
    pub.start();

    // Wait for at least 2 heartbeat intervals.
    std::this_thread::sleep_for(kHeartbeatInterval * 3);

    EXPECT_GE(broker.pingReqCount(), 1U)
        << "heartbeat timer should have fired at least once";

    pub.stop();
}

// Clean disconnect

TEST(MqttClientTest, StopSendsDisconnectFrame) {
    MockMqttBroker broker;
    MqttClient pub(makeConfig(broker.port()));
    pub.start();
    std::this_thread::sleep_for(kBrokerSettleDelay);

    pub.stop();
    std::this_thread::sleep_for(kBrokerSettleDelay);

    EXPECT_TRUE(broker.sawDisconnect())
        << "stop() should send a clean DISCONNECT before closing";
}

// Subscribe side

TEST(MqttClientTest, SubscribeSendsSubscribeFrameAfterStart) {
    MockMqttBroker broker;
    MqttClient client(makeConfig(broker.port()));

    client.start();
    std::this_thread::sleep_for(kBrokerSettleDelay);

    client.subscribe("sensors/equipment/0/state",
                     [](std::string_view, std::string_view) {});

    std::this_thread::sleep_for(kBrokerSettleDelay);

    EXPECT_TRUE(broker.sawSubscribe());
    EXPECT_EQ(broker.lastSubscribedTopic(), "sensors/equipment/0/state");

    client.stop();
}

TEST(MqttClientTest, SubscribeBeforeStartReplaysAfterConnect) {
    // Pre-start subscribe should be queued and fired once CONNACK arrives.
    MockMqttBroker broker;
    MqttClient client(makeConfig(broker.port()));

    client.subscribe("queued/topic",
                     [](std::string_view, std::string_view) {});

    client.start();
    std::this_thread::sleep_for(kBrokerSettleDelay);

    EXPECT_TRUE(broker.sawSubscribe());
    EXPECT_EQ(broker.lastSubscribedTopic(), "queued/topic");

    client.stop();
}

TEST(MqttClientTest, InboundPublishInvokesMatchingCallback) {
    MockMqttBroker broker;
    broker.echoOnNextSubscribe("sensors/T", "hello-from-broker");

    MqttClient client(makeConfig(broker.port()));

    std::atomic<int> hits{0};
    std::string capturedTopic;
    std::string capturedPayload;
    std::mutex captureMutex;

    client.subscribe(
        "sensors/T",
        [&](std::string_view topic, std::string_view payload) {
            const std::scoped_lock lock(captureMutex);
            capturedTopic.assign(topic);
            capturedPayload.assign(payload);
            hits.fetch_add(1, std::memory_order_release);
        });

    client.start();
    // Wait for the broker to receive SUBSCRIBE, send SUBACK, then
    // PUBLISH the echo. The client's read loop dispatches on the
    // io_context worker thread.
    std::this_thread::sleep_for(kPublishSettleDelay);

    EXPECT_EQ(hits.load(std::memory_order_acquire), 1);
    {
        const std::scoped_lock lock(captureMutex);
        EXPECT_EQ(capturedTopic, "sensors/T");
        EXPECT_EQ(capturedPayload, "hello-from-broker");
    }
    EXPECT_EQ(client.receivedCount(), 1U);

    client.stop();
}

TEST(MqttClientTest, InboundPublishOnUnknownTopicDoesNotCallback) {
    // Broker pushes a PUBLISH on a topic we never subscribed to.
    // The client should still count it as received (read loop drained
    // the frame off the wire) but invoke no callback.
    MockMqttBroker broker;
    broker.echoOnNextSubscribe("unrelated/topic", "ignored");

    MqttClient client(makeConfig(broker.port()));

    std::atomic<int> hits{0};
    client.subscribe(
        "subscribed/topic",
        [&](std::string_view, std::string_view) {
            hits.fetch_add(1, std::memory_order_release);
        });

    client.start();
    std::this_thread::sleep_for(kPublishSettleDelay);

    EXPECT_EQ(hits.load(std::memory_order_acquire), 0);
    EXPECT_EQ(client.receivedCount(), 1U)
        << "frame was dispatched; only the callback lookup missed";

    client.stop();
}
