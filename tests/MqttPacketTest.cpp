// Tests for the MQTT 3.1.1 wire-format primitives in MqttPacket.h.
//
// Pure C++ logic, no I/O. Each test asserts byte-exact output against
// the spec section that defines the packet format.

#include "src/integration/MqttPacket.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using app::integration::mqtt::buildConnect;
using app::integration::mqtt::buildDisconnect;
using app::integration::mqtt::buildPingReq;
using app::integration::mqtt::buildPublish;
using app::integration::mqtt::ConnAckCode;
using app::integration::mqtt::decodeRemainingLength;
using app::integration::mqtt::encodeRemainingLength;
using app::integration::mqtt::encodeString;
using app::integration::mqtt::kConnAckPacketSize;
using app::integration::mqtt::kMaxRemainingLength;
using app::integration::mqtt::kMaxStringLength;
using app::integration::mqtt::PacketType;
using app::integration::mqtt::parseConnAck;
using app::integration::mqtt::buildSubscribe;
using app::integration::mqtt::buildUnsubscribe;
using app::integration::mqtt::parseSubAck;
using app::integration::mqtt::parseUnsubAck;
using app::integration::mqtt::parsePublish;
using app::integration::mqtt::SubAckCode;
using app::integration::mqtt::ParsedSubAck;
using app::integration::mqtt::ParsedPublish;

namespace {

// Common per-spec byte values used in multiple tests. Pulling them
// into named constants keeps the assertions self-explanatory and
// pacifies clang-tidy's avoid-magic-numbers.
constexpr std::uint8_t kConnectByte    = 0x10U;
constexpr std::uint8_t kPublishByte    = 0x30U;
constexpr std::uint8_t kPingReqByte    = 0xC0U;
constexpr std::uint8_t kDisconnectByte = 0xE0U;
constexpr std::uint8_t kConnAckByte    = 0x20U;
constexpr std::uint8_t kSubscribeByte   = 0x82U;  // Subscribe + 0b0010
constexpr std::uint8_t kSubAckByte      = 0x90U;
constexpr std::uint8_t kUnsubscribeByte = 0xA2U;  // Unsubscribe + 0b0010
constexpr std::uint8_t kUnsubAckByte    = 0xB0U;

constexpr std::uint8_t kEmptyRemaining = 0x00U;
constexpr std::uint8_t kProtocolLevel  = 0x04U;  // MQTT 3.1.1
constexpr std::uint8_t kCleanSession   = 0x02U;

}  // namespace

// Remaining-length encoding (spec section 2.2.3)

TEST(MqttPacketTest, EncodeRemainingLengthSingleByteForSmallValues) {
    EXPECT_EQ(encodeRemainingLength(0),
              std::vector<std::uint8_t>{0x00});
    EXPECT_EQ(encodeRemainingLength(1),
              std::vector<std::uint8_t>{0x01});
    EXPECT_EQ(encodeRemainingLength(127),
              std::vector<std::uint8_t>{0x7F});
}

TEST(MqttPacketTest, EncodeRemainingLengthTwoBytesAt128) {
    // 128 = 0x80 0x01: continuation bit on first byte (so consumer
    // reads another), value 1 in the second byte * 128 = 128.
    EXPECT_EQ(encodeRemainingLength(128),
              (std::vector<std::uint8_t>{0x80, 0x01}));
    EXPECT_EQ(encodeRemainingLength(16'383),
              (std::vector<std::uint8_t>{0xFF, 0x7F}));
}

TEST(MqttPacketTest, EncodeRemainingLengthThreeBytesAt16384) {
    EXPECT_EQ(encodeRemainingLength(16'384),
              (std::vector<std::uint8_t>{0x80, 0x80, 0x01}));
}

TEST(MqttPacketTest, EncodeRemainingLengthFourBytesAt2097152) {
    EXPECT_EQ(encodeRemainingLength(2'097'152),
              (std::vector<std::uint8_t>{0x80, 0x80, 0x80, 0x01}));
}

TEST(MqttPacketTest, EncodeRemainingLengthMaxAllowed) {
    EXPECT_NO_THROW((void)encodeRemainingLength(kMaxRemainingLength));
}

TEST(MqttPacketTest, EncodeRemainingLengthThrowsAboveMax) {
    EXPECT_THROW((void)encodeRemainingLength(kMaxRemainingLength + 1),
                 std::invalid_argument);
}

TEST(MqttPacketTest, DecodeRemainingLengthRoundTripsAcrossBoundaries) {
    for (std::uint32_t v :
         {0U, 1U, 127U, 128U, 16'383U, 16'384U, 2'097'151U, 2'097'152U,
          kMaxRemainingLength}) {
        const auto encoded = encodeRemainingLength(v);
        std::size_t cursor = 0;
        EXPECT_EQ(decodeRemainingLength(encoded, cursor), v) << "value=" << v;
        EXPECT_EQ(cursor, encoded.size())
            << "decode should consume the entire variable-length sequence";
    }
}

TEST(MqttPacketTest, DecodeRemainingLengthThrowsOnTruncatedInput) {
    // 0x80 alone has continuation bit set but no follow-up byte.
    const std::vector<std::uint8_t> truncated{0x80};
    std::size_t cursor = 0;
    EXPECT_THROW((void)decodeRemainingLength(truncated, cursor),
                 std::runtime_error);
}

TEST(MqttPacketTest, DecodeRemainingLengthThrowsOnFiveByteSequence) {
    // Five bytes with continuation bit set is malformed per spec.
    const std::vector<std::uint8_t> bad{0x80, 0x80, 0x80, 0x80, 0x01};
    std::size_t cursor = 0;
    EXPECT_THROW((void)decodeRemainingLength(bad, cursor),
                 std::runtime_error);
}

// String encoding (spec section 1.5.3)

TEST(MqttPacketTest, EncodeStringEmptyHasZeroLengthPrefix) {
    EXPECT_EQ(encodeString(""),
              (std::vector<std::uint8_t>{0x00, 0x00}));
}

TEST(MqttPacketTest, EncodeStringPrefixesLengthBigEndian) {
    // "MQTT" -> length 4, then bytes 'M','Q','T','T'.
    const auto bytes = encodeString("MQTT");
    EXPECT_EQ(bytes,
              (std::vector<std::uint8_t>{0x00, 0x04, 'M', 'Q', 'T', 'T'}));
}

TEST(MqttPacketTest, EncodeStringThrowsAboveMaxLength) {
    const std::string tooLong(kMaxStringLength + 1, 'x');
    EXPECT_THROW((void)encodeString(tooLong), std::invalid_argument);
}

// CONNECT (spec section 3.1)

TEST(MqttPacketTest, BuildConnectStartsWithConnectFixedHeader) {
    const auto bytes = buildConnect("client-1", std::chrono::seconds{60});
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes[0], kConnectByte);
}

TEST(MqttPacketTest, BuildConnectIncludesProtocolNameMqttAndLevel4) {
    const auto bytes = buildConnect("c", std::chrono::seconds{60});
    // After fixed header (1) + remaining length (1), variable header
    // begins with the protocol-name length-prefixed string.
    // bytes[2..7] = 0x00 0x04 'M' 'Q' 'T' 'T'
    ASSERT_GT(bytes.size(), 8U);
    EXPECT_EQ(bytes[2], 0x00);
    EXPECT_EQ(bytes[3], 0x04);
    EXPECT_EQ(bytes[4], 'M');
    EXPECT_EQ(bytes[5], 'Q');
    EXPECT_EQ(bytes[6], 'T');
    EXPECT_EQ(bytes[7], 'T');
    // Protocol level (1 byte after the protocol name).
    EXPECT_EQ(bytes[8], kProtocolLevel);
}

TEST(MqttPacketTest, BuildConnectSetsCleanSessionFlag) {
    const auto bytes = buildConnect("c", std::chrono::seconds{60});
    // Connect flags is 1 byte after the protocol level: bytes[9].
    ASSERT_GT(bytes.size(), 9U);
    EXPECT_EQ(bytes[9], kCleanSession);
}

TEST(MqttPacketTest, BuildConnectEncodesKeepAliveBigEndian) {
    const auto bytes = buildConnect("c", std::chrono::seconds{300});
    // Keep alive is bytes[10..11], big-endian. 300 = 0x012C.
    ASSERT_GT(bytes.size(), 11U);
    EXPECT_EQ(bytes[10], 0x01);
    EXPECT_EQ(bytes[11], 0x2C);
}

TEST(MqttPacketTest, BuildConnectIncludesClientIdInPayload) {
    const auto bytes = buildConnect("hmi-42", std::chrono::seconds{30});
    // Client ID is the only payload field after the variable header.
    // We can find it as the trailing length-prefixed string.
    ASSERT_GE(bytes.size(), 14U);
    EXPECT_EQ(bytes[bytes.size() - 6], 'h');
    EXPECT_EQ(bytes[bytes.size() - 5], 'm');
    EXPECT_EQ(bytes[bytes.size() - 4], 'i');
}

TEST(MqttPacketTest, BuildConnectThrowsOnNegativeKeepAlive) {
    EXPECT_THROW((void)buildConnect("c", std::chrono::seconds{-1}),
                 std::invalid_argument);
}

TEST(MqttPacketTest, BuildConnectThrowsAboveKeepAliveMax) {
    EXPECT_THROW((void)buildConnect("c", std::chrono::seconds{65'536}),
                 std::invalid_argument);
}

// PUBLISH (spec section 3.3)

TEST(MqttPacketTest, BuildPublishStartsWithPublishFixedHeader) {
    const auto bytes = buildPublish("topic/a", "payload");
    ASSERT_FALSE(bytes.empty());
    // QoS 0 publish: low nibble flags are 0 -> byte equals exactly 0x30.
    EXPECT_EQ(bytes[0], kPublishByte);
}

TEST(MqttPacketTest, BuildPublishEncodesTopicAsLengthPrefixedString) {
    const auto bytes = buildPublish("ab", "payload");
    // bytes[2..3] = topic length (0x00 0x02), bytes[4..5] = "ab".
    ASSERT_GT(bytes.size(), 5U);
    EXPECT_EQ(bytes[2], 0x00);
    EXPECT_EQ(bytes[3], 0x02);
    EXPECT_EQ(bytes[4], 'a');
    EXPECT_EQ(bytes[5], 'b');
}

TEST(MqttPacketTest, BuildPublishAppendsPayloadVerbatim) {
    const auto bytes = buildPublish("t", "hello");
    // After 1 (fixed) + 1 (remaining) + 2 (topic length) + 1 (topic) = 5 bytes
    // of header, the payload starts at offset 5.
    ASSERT_EQ(bytes.size(), 10U);
    EXPECT_EQ(bytes[5], 'h');
    EXPECT_EQ(bytes[6], 'e');
    EXPECT_EQ(bytes[7], 'l');
    EXPECT_EQ(bytes[8], 'l');
    EXPECT_EQ(bytes[9], 'o');
}

TEST(MqttPacketTest, BuildPublishWithEmptyPayloadIsLegal) {
    const auto bytes = buildPublish("t", "");
    EXPECT_EQ(bytes[0], kPublishByte);
    // Remaining length = 2 (topic length) + 1 (topic byte) + 0 = 3.
    EXPECT_EQ(bytes[1], 0x03);
}

// PINGREQ / DISCONNECT (sections 3.12 / 3.14)

TEST(MqttPacketTest, BuildPingReqIsTwoFixedBytes) {
    EXPECT_EQ(buildPingReq(),
              (std::vector<std::uint8_t>{kPingReqByte, kEmptyRemaining}));
}

TEST(MqttPacketTest, BuildDisconnectIsTwoFixedBytes) {
    EXPECT_EQ(buildDisconnect(),
              (std::vector<std::uint8_t>{kDisconnectByte, kEmptyRemaining}));
}

// CONNACK parsing (section 3.2)

TEST(MqttPacketTest, ParseConnAckAcceptedReturnCode) {
    const std::vector<std::uint8_t> bytes{
        kConnAckByte, 0x02, 0x00, 0x00};
    EXPECT_EQ(parseConnAck(bytes), ConnAckCode::Accepted);
}

TEST(MqttPacketTest, ParseConnAckMapsAllReturnCodes) {
    for (std::uint8_t code = 0; code <= 0x05U; ++code) {
        const std::vector<std::uint8_t> bytes{
            kConnAckByte, 0x02, 0x00, code};
        EXPECT_EQ(static_cast<std::uint8_t>(parseConnAck(bytes)), code);
    }
}

TEST(MqttPacketTest, ParseConnAckThrowsOnTruncatedBuffer) {
    const std::vector<std::uint8_t> tooShort{kConnAckByte, 0x02, 0x00};
    EXPECT_THROW((void)parseConnAck(tooShort), std::runtime_error);
}

TEST(MqttPacketTest, ParseConnAckThrowsOnWrongFixedHeader) {
    // 0x10 is CONNECT, not CONNACK -- different direction.
    const std::vector<std::uint8_t> wrongType{0x10, 0x02, 0x00, 0x00};
    EXPECT_THROW((void)parseConnAck(wrongType), std::runtime_error);
}

TEST(MqttPacketTest, ParseConnAckThrowsOnBadRemainingLength) {
    const std::vector<std::uint8_t> wrongLen{kConnAckByte, 0x03, 0x00, 0x00};
    EXPECT_THROW((void)parseConnAck(wrongLen), std::runtime_error);
}

TEST(MqttPacketTest, ConnAckPacketSizeMatchesSpec) {
    // Sanity check: spec says CONNACK is exactly 4 bytes.
    EXPECT_EQ(kConnAckPacketSize, 4U);
}

// SUBSCRIBE / SUBACK / UNSUBSCRIBE / UNSUBACK (spec sections 3.8 - 3.11)

TEST(MqttPacketTest, BuildSubscribeStartsWithReservedFlagsHeader) {
    // Section 3.8.1: SUBSCRIBE fixed-header low nibble must equal
    // 0b0010 -- brokers reject anything else.
    const auto bytes = buildSubscribe(1, "topic");
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes[0], kSubscribeByte);
}

TEST(MqttPacketTest, BuildSubscribeCarriesPacketIdBigEndian) {
    constexpr std::uint16_t kPacketId = 0x1234U;
    const auto bytes = buildSubscribe(kPacketId, "x");
    // Skip fixed header (1) + remaining-length byte (1) -> variable
    // header starts at offset 2 with packet identifier MSB then LSB.
    ASSERT_GE(bytes.size(), 4U);
    EXPECT_EQ(bytes[2], 0x12U);
    EXPECT_EQ(bytes[3], 0x34U);
}

TEST(MqttPacketTest, BuildSubscribeAppendsTopicAndQos0) {
    const auto bytes = buildSubscribe(1, "ab");
    // After packet id (2B) we expect string length prefix (0x00 0x02),
    // then "ab", then the requested-QoS byte (0x00).
    ASSERT_EQ(bytes.size(), 9U);  // 1+1+2+2+2+1
    EXPECT_EQ(bytes[4], 0x00U);
    EXPECT_EQ(bytes[5], 0x02U);
    EXPECT_EQ(bytes[6], 'a');
    EXPECT_EQ(bytes[7], 'b');
    EXPECT_EQ(bytes[8], 0x00U);  // QoS 0
}

TEST(MqttPacketTest, BuildUnsubscribeStartsWithReservedFlagsHeader) {
    const auto bytes = buildUnsubscribe(1, "topic");
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes[0], kUnsubscribeByte);
}

TEST(MqttPacketTest, BuildUnsubscribeOmitsQosByte) {
    // UNSUBSCRIBE payload is just the topic filter -- no QoS byte
    // unlike SUBSCRIBE (spec section 3.10.3).
    const auto bytes = buildUnsubscribe(1, "ab");
    ASSERT_EQ(bytes.size(), 8U);  // 1 + 1 + 2 + 2 + 2 (no QoS trailing byte)
}

TEST(MqttPacketTest, ParseSubAckRoundTripsGrantedQos0) {
    constexpr std::uint16_t kPacketId = 0x4242U;
    const std::vector<std::uint8_t> wire{
        kSubAckByte, 0x03,
        0x42, 0x42,                                       // packet id
        static_cast<std::uint8_t>(SubAckCode::GrantedQos0)
    };
    const auto parsed = parseSubAck(wire);
    EXPECT_EQ(parsed.packetId, kPacketId);
    EXPECT_EQ(parsed.returnCode, SubAckCode::GrantedQos0);
}

TEST(MqttPacketTest, ParseSubAckSurfacesBrokerFailure) {
    const std::vector<std::uint8_t> wire{
        kSubAckByte, 0x03, 0x00, 0x01,
        static_cast<std::uint8_t>(SubAckCode::Failure)
    };
    EXPECT_EQ(parseSubAck(wire).returnCode, SubAckCode::Failure);
}

TEST(MqttPacketTest, ParseSubAckThrowsOnTruncatedBuffer) {
    const std::vector<std::uint8_t> tooShort{kSubAckByte, 0x03, 0x00};
    EXPECT_THROW((void)parseSubAck(tooShort), std::runtime_error);
}

TEST(MqttPacketTest, ParseSubAckThrowsOnWrongHeader) {
    const std::vector<std::uint8_t> wrong{0x40, 0x03, 0x00, 0x01, 0x00};
    EXPECT_THROW((void)parseSubAck(wrong), std::runtime_error);
}

TEST(MqttPacketTest, ParseUnsubAckRoundTripsPacketId) {
    constexpr std::uint16_t kPacketId = 0x00ABU;
    const std::vector<std::uint8_t> wire{kUnsubAckByte, 0x02, 0x00, 0xAB};
    EXPECT_EQ(parseUnsubAck(wire), kPacketId);
}

TEST(MqttPacketTest, ParseUnsubAckThrowsOnTruncatedBuffer) {
    const std::vector<std::uint8_t> tooShort{kUnsubAckByte, 0x02, 0x00};
    EXPECT_THROW((void)parseUnsubAck(tooShort), std::runtime_error);
}

// Incoming PUBLISH (subscriber side)

TEST(MqttPacketTest, ParsePublishRoundTripsTopicAndPayload) {
    // Build a PUBLISH the way the broker would, then parse it back.
    const auto wire = buildPublish("sensors/temp", "42.5");
    const ParsedPublish parsed = parsePublish(wire);
    EXPECT_EQ(parsed.topic, "sensors/temp");
    EXPECT_EQ(parsed.payload, "42.5");
}

TEST(MqttPacketTest, ParsePublishAcceptsEmptyPayload) {
    const auto wire = buildPublish("heartbeat", "");
    const ParsedPublish parsed = parsePublish(wire);
    EXPECT_EQ(parsed.topic, "heartbeat");
    EXPECT_TRUE(parsed.payload.empty());
}

TEST(MqttPacketTest, ParsePublishThrowsOnNonPublishHeader) {
    // 0x10 is CONNECT, not PUBLISH.
    const std::vector<std::uint8_t> wrongType{
        0x10, 0x02, 0x00, 0x00
    };
    EXPECT_THROW((void)parsePublish(wrongType), std::runtime_error);
}

TEST(MqttPacketTest, ParsePublishRejectsQos1AndAbove) {
    // 0x32 = PUBLISH with QoS=1 in the flag nibble. Not supported.
    const std::vector<std::uint8_t> qos1Frame{
        0x32, 0x04,
        0x00, 0x01, 't',  // tiny topic
        0x00              // dangling byte stands in for a payload
    };
    EXPECT_THROW((void)parsePublish(qos1Frame), std::runtime_error);
}

TEST(MqttPacketTest, ParsePublishThrowsOnTruncatedBody) {
    // Remaining length claims 10 bytes but only 4 follow.
    const std::vector<std::uint8_t> truncated{
        0x30, 0x0A,
        0x00, 0x02, 'a', 'b'
    };
    EXPECT_THROW((void)parsePublish(truncated), std::runtime_error);
}

TEST(MqttPacketTest, ParsePublishThrowsOnEmptyBuffer) {
    EXPECT_THROW((void)parsePublish({}), std::runtime_error);
}
