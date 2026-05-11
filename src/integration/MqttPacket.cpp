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

/// SUBSCRIBE and UNSUBSCRIBE require the fixed-header low nibble to
/// equal 0b0010 (sections 3.8.1 / 3.10.1). Brokers reject frames that
/// don't carry exactly this value.
constexpr std::uint8_t kSubscribeFixedHeaderFlags   = 0x02U;
constexpr std::uint8_t kUnsubscribeFixedHeaderFlags = 0x02U;

/// SUBSCRIBE payload entry: each topic filter is followed by a single
/// byte holding the requested QoS (section 3.8.3). We only ask for
/// QoS 0.
constexpr std::uint8_t kSubscribeRequestedQos0 = 0x00U;

/// SUBACK for one topic filter: 2-byte packet identifier + 1-byte
/// return code = 3 bytes of remaining length.
constexpr std::uint8_t kSubAckSingleTopicRemainingLength = 0x03U;

/// SUBACK total size on the wire = fixed header (1) + remaining
/// length byte (1) + 3-byte body.
constexpr std::size_t kSubAckPacketSize = 5U;

/// UNSUBACK remaining length is always 2 (packet identifier only).
constexpr std::uint8_t kUnsubAckRemainingLength = 0x02U;

/// UNSUBACK total size on the wire = fixed header (1) + remaining
/// length byte (1) + 2-byte body.
constexpr std::size_t kUnsubAckPacketSize = 4U;

/// PUBLISH fixed-header low-nibble mask covering DUP/QoS/RETAIN
/// (section 3.3.1). We accept only QoS 0 frames; the other combinations
/// would require additional state (PUBACK/PUBREC).
constexpr std::uint8_t kPublishFlagsMask  = 0x0FU;
constexpr std::uint8_t kPublishQosShift   = 1U;
constexpr std::uint8_t kPublishQosMask    = 0x03U;

/// Mask for the upper nibble of an MQTT fixed-header byte -- isolates
/// the packet-type bits from the low-nibble flag bits.
constexpr std::uint8_t kFixedHeaderTypeMask = 0xF0U;

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

/// Read a uint16_t in big-endian order starting at `cursor`, advancing
/// `cursor` by 2. Throws if the buffer is too short.
std::uint16_t readUint16BigEndian(const std::vector<std::uint8_t>& bytes,
                                  std::size_t& cursor) {
    constexpr unsigned kHighShift = 8U;
    if (cursor + 1 >= bytes.size()) {
        throw std::runtime_error(
            "MQTT: truncated buffer reading 2-byte field");
    }
    const auto high = static_cast<unsigned>(bytes[cursor++]);
    const auto low  = static_cast<unsigned>(bytes[cursor++]);
    return static_cast<std::uint16_t>((high << kHighShift) | low);
}

/// Read a length-prefixed UTF-8 string (2-byte BE length + bytes)
/// starting at `cursor`. Advances `cursor` past the field. Throws on
/// truncation.
std::string readString(const std::vector<std::uint8_t>& bytes,
                       std::size_t& cursor) {
    const auto len = readUint16BigEndian(bytes, cursor);
    if (cursor + len > bytes.size()) {
        throw std::runtime_error(std::format(
            "MQTT: truncated string field (need {} bytes, have {})",
            len, bytes.size() - cursor));
    }
    std::string s;
    s.reserve(len);
    for (std::uint16_t i = 0; i < len; ++i) {
        s.push_back(static_cast<char>(bytes[cursor++]));
    }
    return s;
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

std::vector<std::uint8_t> buildSubscribe(std::uint16_t packetId,
                                         const std::string& topic) {
    // Variable header: 2-byte packet identifier.
    std::vector<std::uint8_t> variable;
    appendUint16BigEndian(variable, packetId);

    // Payload: one (topic filter, requested QoS) pair. Multi-topic
    // SUBSCRIBE just appends more pairs; we keep this surface minimal.
    const auto topicBytes = encodeString(topic);
    std::vector<std::uint8_t> payload;
    payload.insert(payload.end(), topicBytes.begin(), topicBytes.end());
    payload.push_back(kSubscribeRequestedQos0);

    const auto remaining =
        static_cast<std::uint32_t>(variable.size() + payload.size());

    std::vector<std::uint8_t> out;
    out.push_back(static_cast<std::uint8_t>(PacketType::Subscribe) |
                  kSubscribeFixedHeaderFlags);
    const auto remainingBytes = encodeRemainingLength(remaining);
    out.insert(out.end(), remainingBytes.begin(), remainingBytes.end());
    out.insert(out.end(), variable.begin(), variable.end());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<std::uint8_t> buildUnsubscribe(std::uint16_t packetId,
                                           const std::string& topic) {
    std::vector<std::uint8_t> variable;
    appendUint16BigEndian(variable, packetId);

    // Payload: one topic filter, no QoS byte (UNSUBSCRIBE doesn't
    // need it -- the broker drops the subscription regardless).
    const auto topicBytes = encodeString(topic);

    const auto remaining =
        static_cast<std::uint32_t>(variable.size() + topicBytes.size());

    std::vector<std::uint8_t> out;
    out.push_back(static_cast<std::uint8_t>(PacketType::Unsubscribe) |
                  kUnsubscribeFixedHeaderFlags);
    const auto remainingBytes = encodeRemainingLength(remaining);
    out.insert(out.end(), remainingBytes.begin(), remainingBytes.end());
    out.insert(out.end(), variable.begin(), variable.end());
    out.insert(out.end(), topicBytes.begin(), topicBytes.end());
    return out;
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

ParsedSubAck parseSubAck(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < kSubAckPacketSize) {
        throw std::runtime_error(std::format(
            "MQTT SUBACK truncated ({} bytes, need {})",
            bytes.size(), kSubAckPacketSize));
    }
    if (bytes[0] != static_cast<std::uint8_t>(PacketType::SubAck)) {
        throw std::runtime_error(std::format(
            "MQTT SUBACK: bad fixed header byte 0x{:02x}", bytes[0]));
    }
    if (bytes[1] != kSubAckSingleTopicRemainingLength) {
        throw std::runtime_error(std::format(
            "MQTT SUBACK: bad remaining length 0x{:02x} "
            "(only single-topic SUBACK supported, expected 0x{:02x})",
            bytes[1], kSubAckSingleTopicRemainingLength));
    }
    std::size_t cursor = 2;
    const auto packetId = readUint16BigEndian(bytes, cursor);
    const auto code     = static_cast<SubAckCode>(bytes[cursor]);
    return ParsedSubAck{packetId, code};
}

std::uint16_t parseUnsubAck(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < kUnsubAckPacketSize) {
        throw std::runtime_error(std::format(
            "MQTT UNSUBACK truncated ({} bytes, need {})",
            bytes.size(), kUnsubAckPacketSize));
    }
    if (bytes[0] != static_cast<std::uint8_t>(PacketType::UnsubAck)) {
        throw std::runtime_error(std::format(
            "MQTT UNSUBACK: bad fixed header byte 0x{:02x}", bytes[0]));
    }
    if (bytes[1] != kUnsubAckRemainingLength) {
        throw std::runtime_error(std::format(
            "MQTT UNSUBACK: bad remaining length 0x{:02x} (expected 0x{:02x})",
            bytes[1], kUnsubAckRemainingLength));
    }
    std::size_t cursor = 2;
    return readUint16BigEndian(bytes, cursor);
}

ParsedPublish parsePublish(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) {
        throw std::runtime_error("MQTT PUBLISH: empty buffer");
    }
    const auto fixedHeader = bytes[0];
    if ((fixedHeader & kFixedHeaderTypeMask) !=
            static_cast<std::uint8_t>(PacketType::Publish)) {
        throw std::runtime_error(std::format(
            "MQTT PUBLISH: bad fixed header byte 0x{:02x}", fixedHeader));
    }
    // Cast to unsigned before bit-shifting so hicpp-signed-bitwise stays
    // quiet (uint8_t promotes to int otherwise).
    const auto flagsNibble = static_cast<unsigned>(fixedHeader) &
                             static_cast<unsigned>(kPublishFlagsMask);
    const auto qos = (flagsNibble >> kPublishQosShift) &
                     static_cast<unsigned>(kPublishQosMask);
    if (qos != 0) {
        // We don't track packet identifiers for QoS 1/2; subscriber
        // would owe PUBACK/PUBREC otherwise. The simpler scope is
        // enough for the current portfolio narrative.
        throw std::runtime_error(std::format(
            "MQTT PUBLISH QoS {} not supported (QoS 0 only)", qos));
    }

    std::size_t cursor = 1;
    const auto remaining = decodeRemainingLength(bytes, cursor);
    const auto bodyStart = cursor;
    const auto bodyEnd   = bodyStart + remaining;
    if (bodyEnd > bytes.size()) {
        throw std::runtime_error(std::format(
            "MQTT PUBLISH: body truncated (need {} bytes, have {})",
            remaining, bytes.size() - bodyStart));
    }

    ParsedPublish out;
    out.topic = readString(bytes, cursor);
    // For QoS 0 there's no packet identifier; payload occupies the
    // remaining bytes verbatim.
    out.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                       bytes.begin() + static_cast<std::ptrdiff_t>(bodyEnd));
    return out;
}

}  // namespace app::integration::mqtt
