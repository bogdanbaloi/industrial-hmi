#pragma once

// MQTT 3.1.1 wire-format primitives -- hand-rolled per spec
// (OASIS MQTT 3.1.1, OASIS Standard 29 October 2014).
//
// Why hand-rolled (no paho-mqtt, no mqtt_cpp)?
//   * Pulls zero new system packages on either Linux or MSYS2 -- the
//     project's CI already passes on stock distros and stays that way.
//   * Demonstrates understanding of the wire format itself, not just
//     plumbing of a third-party client. The published topics in this
//     project are simple key=value telemetry; reaching for a full
//     MQTT 5.0 client to publish 4 strings would be over-engineering.
//   * Publish-only scope keeps the surface tiny: CONNECT, CONNACK,
//     PUBLISH, PINGREQ, PINGRESP, DISCONNECT. ~200 lines of binary
//     framing rather than a 5000-line client library.
//
// Limitations of this implementation (deliberate):
//   * MQTT 3.1.1 only. No 5.0 properties, no shared subscriptions.
//   * QoS 0 PUBLISH only (fire-and-forget). No QoS 1/2 acknowledgement
//     state machines, no in-flight tracking, no message IDs.
//   * No SUBSCRIBE / SUBACK / UNSUBSCRIBE -- this is a publisher.
//     A subscriber implementation would need PUBACK/PUBREC/PUBREL/
//     PUBCOMP for QoS > 0 and is left for a future PR.
//   * No TLS. Bare TCP only. Production deployments would tunnel
//     through stunnel or similar; the wire format below stays the same.

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace app::integration::mqtt {

/// MQTT control packet types (high nibble of the fixed header byte).
/// Values per spec section 2.2.1 "MQTT Control Packet type".
enum class PacketType : std::uint8_t {
    Connect     = 0x10,
    ConnAck     = 0x20,
    Publish     = 0x30,
    Subscribe   = 0x80,
    SubAck      = 0x90,
    Unsubscribe = 0xA0,
    UnsubAck    = 0xB0,
    PingReq     = 0xC0,
    PingResp    = 0xD0,
    Disconnect  = 0xE0,
};

/// SUBACK return codes per MQTT 3.1.1 section 3.9.3. A broker emits
/// one of these per topic filter in the matching SUBSCRIBE. The first
/// three describe the maximum QoS granted; the failure code indicates
/// the broker refused (e.g. ACL denied).
enum class SubAckCode : std::uint8_t {
    GrantedQos0 = 0x00,
    GrantedQos1 = 0x01,
    GrantedQos2 = 0x02,
    Failure     = 0x80,
};

/// CONNACK return codes per MQTT 3.1.1 section 3.2.2.3.
enum class ConnAckCode : std::uint8_t {
    Accepted                    = 0x00,
    UnacceptableProtocolVersion = 0x01,
    IdentifierRejected          = 0x02,
    ServerUnavailable           = 0x03,
    BadUsernameOrPassword       = 0x04,
    NotAuthorized               = 0x05,
};

/// Maximum value the variable-length "remaining length" field can hold
/// (MQTT 3.1.1 section 2.2.3). Above this the encoding overflows the
/// 4-byte ceiling.
inline constexpr std::uint32_t kMaxRemainingLength = 268'435'455U;

/// Maximum length of UTF-8 strings in MQTT packets -- the 2-byte
/// length prefix caps payload at 65535 bytes (spec section 1.5.3).
inline constexpr std::size_t kMaxStringLength = 0xFFFFU;

/// Byte size of a CONNACK packet on the wire (fixed header + 2 byte
/// remaining length payload).
inline constexpr std::size_t kConnAckPacketSize = 4U;

/// Encode an unsigned integer (0..kMaxRemainingLength) per MQTT 3.1.1
/// section 2.2.3 "Remaining Length" -- the variable-length encoding
/// used in every fixed header. 1..4 bytes; each byte stores 7 bits of
/// value in the lower bits and "more bytes follow" in bit 7.
///
/// Throws std::invalid_argument on values larger than kMaxRemainingLength.
[[nodiscard]] std::vector<std::uint8_t>
    encodeRemainingLength(std::uint32_t value);

/// Inverse of encodeRemainingLength. `cursor` is advanced past the
/// consumed bytes. Throws std::runtime_error on malformed input
/// (more than 4 bytes with continuation bit still set).
[[nodiscard]] std::uint32_t
    decodeRemainingLength(const std::vector<std::uint8_t>& bytes,
                          std::size_t& cursor);

/// Encode a UTF-8 string per MQTT 3.1.1 section 1.5.3 -- 2-byte
/// big-endian length prefix then the raw bytes. Caller is responsible
/// for ensuring the string is valid UTF-8; we don't validate here.
[[nodiscard]] std::vector<std::uint8_t> encodeString(const std::string& s);

/// Build a CONNECT packet with a clean session and no
/// username/password/will. Returns the bytes ready to write to a
/// TCP socket.
///
/// @param clientId   ASCII client identifier (1-23 chars per spec
///                   recommendation, but most brokers accept more).
/// @param keepAlive  Seconds between PINGREQ heartbeats (0..65535).
[[nodiscard]] std::vector<std::uint8_t>
    buildConnect(const std::string& clientId,
                 std::chrono::seconds keepAlive);

/// Build a PUBLISH packet for QoS 0 (no DUP, no RETAIN).
///
/// @param topic    Topic name. Wildcards (`+`, `#`) are NOT valid
///                 in PUBLISH per spec -- this method does not check.
/// @param payload  Raw byte payload. Empty payloads are legal.
[[nodiscard]] std::vector<std::uint8_t>
    buildPublish(const std::string& topic, const std::string& payload);

/// PINGREQ is a 2-byte fixed packet: PingReq + remaining length 0.
[[nodiscard]] std::vector<std::uint8_t> buildPingReq();

/// DISCONNECT is a 2-byte fixed packet: Disconnect + remaining length 0.
/// The client is expected to close the TCP connection immediately
/// after sending.
[[nodiscard]] std::vector<std::uint8_t> buildDisconnect();

/// Parse the 4-byte CONNACK packet from a broker response.
/// Throws std::runtime_error on malformed input (wrong type byte,
/// wrong remaining length, truncated buffer).
[[nodiscard]] ConnAckCode
    parseConnAck(const std::vector<std::uint8_t>& bytes);

/// Build a SUBSCRIBE packet for one topic filter (spec section 3.8).
/// QoS 0 only -- we don't track acknowledgement state for higher QoS.
///
/// @param packetId  16-bit identifier the broker will echo in SUBACK;
///                  caller supplies a unique value per inflight
///                  SUBSCRIBE so the response can be correlated.
/// @param topic     Topic filter (may include `+` / `#` wildcards).
[[nodiscard]] std::vector<std::uint8_t>
    buildSubscribe(std::uint16_t packetId, const std::string& topic);

/// Build an UNSUBSCRIBE packet for one topic filter (spec section 3.10).
///
/// @param packetId  16-bit identifier the broker will echo in UNSUBACK.
/// @param topic     Topic filter previously subscribed to.
[[nodiscard]] std::vector<std::uint8_t>
    buildUnsubscribe(std::uint16_t packetId, const std::string& topic);

/// Parsed SUBACK response (spec section 3.9). Holds the broker's
/// echoed packet identifier and the granted QoS code for the single
/// topic filter we sent. Multi-topic SUBSCRIBE would carry more codes
/// in the payload; we keep the surface minimal.
struct ParsedSubAck {
    std::uint16_t packetId;
    SubAckCode    returnCode;
};

/// Parse a SUBACK packet from a broker response. Single-topic only
/// (matching what `buildSubscribe` emits). Throws on bad header byte,
/// truncated buffer, or wrong remaining length.
[[nodiscard]] ParsedSubAck
    parseSubAck(const std::vector<std::uint8_t>& bytes);

/// Parsed UNSUBACK -- carries only the echoed packet identifier
/// (spec section 3.11; no per-topic codes).
[[nodiscard]] std::uint16_t
    parseUnsubAck(const std::vector<std::uint8_t>& bytes);

/// Parsed incoming PUBLISH frame. Subscribers consume these off the
/// wire and deliver `(topic, payload)` to application callbacks.
/// QoS 0 only: no packet ID, no DUP retry. The struct intentionally
/// keeps owning copies so the callback can outlive the buffer.
struct ParsedPublish {
    std::string topic;
    std::string payload;
};

/// Parse a PUBLISH frame (QoS 0). `bytes` must be the complete packet
/// (fixed header + remaining-length prefix + body). Throws on bad
/// header byte (non-PUBLISH), QoS > 0 flags, or truncation.
[[nodiscard]] ParsedPublish
    parsePublish(const std::vector<std::uint8_t>& bytes);

}  // namespace app::integration::mqtt
