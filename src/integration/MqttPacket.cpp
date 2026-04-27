#include "src/integration/MqttPacket.h"

#include <format>
#include <stdexcept>

namespace app::integration::mqtt {

namespace {

// ===== MQTT 3.1.1 protocol constants (spec section references inline) =====

/// Continuation bit on a remaining-length byte (section 2.2.3).
constexpr std::uint8_t kContinuationBit = 0x80U;

/// Lower 7 bits mask for remaining-length bytes (section 2.2.3).
constexpr std::uint8_t kRemainingLengthValueMask = 0x7FU;

/// Maximum bytes the variable-length encoding can use before the
/// continuation bit cycle is considered malformed (section 2.2.3).
constexpr int kMaxRemainingLengthBytes = 4;

/// Multiplier increment per encoded byte (128 = 2^7, since each byte
/// carries 7 bits of value).
constexpr std::uint32_t kRemainingLengthShift = 128U;

/// Protocol name advertised in the CONNECT variable header
/// (section 3.1.2.1). Always the four ASCII letters "MQTT".
constexpr const char* kProtocolName = "MQTT";

/// Protocol level for MQTT 3.1.1 (section 3.1.2.2). MQTT 5.0 would
/// use 0x05; we only target 3.1.1.
constexpr std::uint8_t kProtocolLevelMqtt311 = 0x04U;

/// Connect flag bit: clean session (section 3.1.2.4).
constexpr std::uint8_t kConnectFlagCleanSession = 0x02U;

/// PUBLISH fixed-header low-nibble flags for QoS 0 transient
/// telemetry: DUP=0, QoS=0, RETAIN=0 (section 3.3.1).
constexpr std::uint8_t kPublishFlagsQos0Transient = 0x00U;

/// Single-byte remaining-length value for empty-body packets
/// (PINGREQ, DISCONNECT, sections 3.12 / 3.14).
constexpr std::uint8_t kEmptyRemainingLength = 0x00U;

/// Remaining-length value of CONNACK -- always 2 bytes (section 3.2.1).
constexpr std::uint8_t kConnAckRemainingLength = 0x02U;

/// Byte index of the return code inside a CONNACK packet
/// (section 3.2.2.3): fixed header (1) + remaining length (1) +
/// connect ack flags (1) = offset 3.
constexpr std::size_t kConnAckReturnCodeOffset = 3U;

/// MQTT 3.1.1 caps keep-alive at 65535 seconds (16-bit field,
/// section 3.1.2.10).
constexpr std::chrono::seconds kMaxKeepAlive{65535};

/// Append a uint16_t in big-endian order. Used everywhere MQTT needs a
/// 2-byte length prefix or a 16-bit field (keep-alive, string length).
void appendUint16BigEndian(std::vector<std::uint8_t>& out, std::uint16_t v) {
    constexpr unsigned kByteMask = 0xFFU;
    constexpr unsigned kHighShift = 8U;
    const unsigned promoted = v;
    out.push_back(static_cast<std::uint8_t>((promoted >> kHighShift) & kByteMask));
    out.push_back(static_cast<std::uint8_t>(promoted & kByteMask));
}

}  // namespace

std::vector<std::uint8_t> encodeRemainingLength(std::uint32_t value) {
    if (value > kMaxRemainingLength) {
        throw std::invalid_argument(std::format(
            "MQTT remaining length {} exceeds protocol max {}",
            value, kMaxRemainingLength));
    }
    // 7 value bits per encoded byte (spec section 2.2.3); top bit
    // is the continuation flag handled below.
    constexpr unsigned kBitsPerByte = 7U;
    std::vector<std::uint8_t> out;
    do {
        std::uint8_t byte = value & kRemainingLengthValueMask;
        value >>= kBitsPerByte;
        if (value > 0) byte |= kContinuationBit;
        out.push_back(byte);
    } while (value > 0);
    return out;
}

std::uint32_t decodeRemainingLength(const std::vector<std::uint8_t>& bytes,
                                    std::size_t& cursor) {
    std::uint32_t multiplier = 1;
    std::uint32_t value = 0;
    for (int i = 0; i < kMaxRemainingLengthBytes; ++i) {
        if (cursor >= bytes.size()) {
            throw std::runtime_error(
                "MQTT remaining length: truncated buffer");
        }
        const std::uint8_t byte = bytes[cursor++];
        value += (byte & kRemainingLengthValueMask) * multiplier;
        if ((byte & kContinuationBit) == 0) {
            return value;
        }
        multiplier *= kRemainingLengthShift;
    }
    throw std::runtime_error(
        "MQTT remaining length: continuation bit set after max bytes");
}

std::vector<std::uint8_t> encodeString(const std::string& s) {
    if (s.size() > kMaxStringLength) {
        throw std::invalid_argument(std::format(
            "MQTT string length {} exceeds 16-bit prefix max {}",
            s.size(), kMaxStringLength));
    }
    std::vector<std::uint8_t> out;
    out.reserve(sizeof(std::uint16_t) + s.size());
    appendUint16BigEndian(out, static_cast<std::uint16_t>(s.size()));
    for (char c : s) out.push_back(static_cast<std::uint8_t>(c));
    return out;
}

std::vector<std::uint8_t> buildConnect(const std::string& clientId,
                                       std::chrono::seconds keepAlive) {
    if (keepAlive.count() < 0 || keepAlive > kMaxKeepAlive) {
        throw std::invalid_argument(std::format(
            "MQTT keep-alive {} out of range [0, {}]",
            keepAlive.count(), kMaxKeepAlive.count()));
    }

    // Variable header: protocol name + level + connect flags + keep-alive.
    std::vector<std::uint8_t> variable;
    const auto protocolName = encodeString(kProtocolName);
    variable.insert(variable.end(), protocolName.begin(), protocolName.end());
    variable.push_back(kProtocolLevelMqtt311);
    variable.push_back(kConnectFlagCleanSession);
    appendUint16BigEndian(variable,
                          static_cast<std::uint16_t>(keepAlive.count()));

    // Payload: client identifier (length-prefixed UTF-8 string).
    const auto payload = encodeString(clientId);

    const auto remaining =
        static_cast<std::uint32_t>(variable.size() + payload.size());

    std::vector<std::uint8_t> out;
    out.push_back(static_cast<std::uint8_t>(PacketType::Connect));
    const auto remainingBytes = encodeRemainingLength(remaining);
    out.insert(out.end(), remainingBytes.begin(), remainingBytes.end());
    out.insert(out.end(), variable.begin(), variable.end());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<std::uint8_t> buildPublish(const std::string& topic,
                                       const std::string& payload) {
    const auto topicBytes = encodeString(topic);
    const auto remaining =
        static_cast<std::uint32_t>(topicBytes.size() + payload.size());

    std::vector<std::uint8_t> out;
    out.push_back(static_cast<std::uint8_t>(PacketType::Publish) |
                  kPublishFlagsQos0Transient);
    const auto remainingBytes = encodeRemainingLength(remaining);
    out.insert(out.end(), remainingBytes.begin(), remainingBytes.end());
    out.insert(out.end(), topicBytes.begin(), topicBytes.end());
    for (char c : payload) out.push_back(static_cast<std::uint8_t>(c));
    return out;
}

std::vector<std::uint8_t> buildPingReq() {
    return {static_cast<std::uint8_t>(PacketType::PingReq),
            kEmptyRemainingLength};
}

std::vector<std::uint8_t> buildDisconnect() {
    return {static_cast<std::uint8_t>(PacketType::Disconnect),
            kEmptyRemainingLength};
}

ConnAckCode parseConnAck(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < kConnAckPacketSize) {
        throw std::runtime_error(std::format(
            "MQTT CONNACK truncated ({} bytes, need {})",
            bytes.size(), kConnAckPacketSize));
    }
    if (bytes[0] != static_cast<std::uint8_t>(PacketType::ConnAck)) {
        throw std::runtime_error(std::format(
            "MQTT CONNACK: bad fixed header byte 0x{:02x}", bytes[0]));
    }
    if (bytes[1] != kConnAckRemainingLength) {
        throw std::runtime_error(std::format(
            "MQTT CONNACK: bad remaining length 0x{:02x} (expected 0x{:02x})",
            bytes[1], kConnAckRemainingLength));
    }
    return static_cast<ConnAckCode>(bytes[kConnAckReturnCodeOffset]);
}

}  // namespace app::integration::mqtt
