// [utest->req~integration-013~1]
// Covers REQ-INTEGRATION-013: the UART flash protocol frame codec and the
// parser that separates frames from telemetry text on one serial stream.
//
// Pure-logic test, no serial port. The byte sequences come from the
// protocol document (docs/protocols/uart-flash-v1.md, section 3), so a
// test failure here means the code and the agreed contract disagree.

#include "src/integration/FlashFrame.h"
#include "src/integration/FlashFrameParser.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using app::integration::crc16CcittFalse;
using app::integration::encodeFlashFrame;
using app::integration::FlashFrame;
using app::integration::FlashFrameParser;
using app::integration::kFlashMaxPayloadBytes;
using app::integration::kFlashStartByte;

/// Message codes used below, from section 4 of the protocol.
constexpr std::uint8_t kInfoRequest = 0x01;
constexpr std::uint8_t kData        = 0x03;
constexpr std::uint8_t kAck         = 0x82;
constexpr std::uint8_t kNak         = 0x83;

/// CRC-16/CCITT-FALSE over the ASCII string below, the standard check value.
constexpr std::string_view kCrcCheckInput = "123456789";
constexpr std::uint16_t    kCrcCheckValue = 0x29B1;

/// Telemetry lines as the board sends them (ADR-0029 format).
constexpr std::string_view kTempLine     = "temp,23.5\n";
constexpr std::string_view kHumidityLine = "humidity,60\n";

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

std::vector<std::byte> ascii(std::string_view text) {
    std::vector<std::byte> out;
    for (const char c : text) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

std::vector<std::byte> concat(std::initializer_list<std::vector<std::byte>> parts) {
    std::vector<std::byte> out;
    for (const auto& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

TEST(FlashFrameParserTest, Crc16ReproducesTheStandardCheckValue) {
    // The one number that proves the right CRC-16 variant was picked.
    EXPECT_EQ(crc16CcittFalse(ascii(kCrcCheckInput)), kCrcCheckValue);
}

TEST(FlashFrameParserTest, EncodeReproducesTheProtocolWorkedExample) {
    // Section 3: INFO_REQ with SEQ 1, then the board's ACK for it.
    EXPECT_EQ(encodeFlashFrame(FlashFrame{kInfoRequest, 1, {}}),
              bytes({0xA5, 0x01, 0x01, 0x00, 0x00, 0x00, 0xE9, 0xCD}));
    EXPECT_EQ(encodeFlashFrame(FlashFrame{kAck, 1, {}}),
              bytes({0xA5, 0x82, 0x01, 0x00, 0x00, 0x00, 0xEB, 0x01}));
}

TEST(FlashFrameParserTest, EncodeRejectsAnOversizedPayload) {
    const FlashFrame tooBig{
        kData, 1, std::vector<std::byte>(kFlashMaxPayloadBytes + 1)};
    EXPECT_THROW((void)encodeFlashFrame(tooBig), std::length_error);
}

TEST(FlashFrameParserTest, DecodesAFrameFedOneByteAtATime) {
    const FlashFrame nak{kNak, 7, bytes({0x01})};
    const std::vector<std::byte> wire = encodeFlashFrame(nak);

    FlashFrameParser parser;
    std::vector<FlashFrame> frames;
    for (const std::byte b : wire) {
        auto out = parser.consume(std::span<const std::byte>(&b, 1));
        EXPECT_TRUE(out.text.empty());
        frames.insert(frames.end(), out.frames.begin(), out.frames.end());
    }
    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(frames[0], nak);
}

TEST(FlashFrameParserTest, PayloadMayHoldNewlinesAndTheStartByte) {
    // A binary payload is read by LEN, so bytes that mean something to the
    // text parser, or that look like a frame start, pass through intact.
    const FlashFrame data{kData, 2,
                          bytes({0x00, 0x00, 0x00, 0x00, 0x0A, 0xA5, 0x0D, 0xFF})};

    FlashFrameParser parser;
    const auto out = parser.consume(encodeFlashFrame(data));
    ASSERT_EQ(out.frames.size(), 1U);
    EXPECT_EQ(out.frames[0], data);
    EXPECT_TRUE(out.text.empty());
    EXPECT_EQ(parser.falseStarts(), 0U);
}

TEST(FlashFrameParserTest, TelemetryTextAroundAFramePassesThroughUnchanged) {
    const FlashFrame ack{kAck, 3, {}};
    const auto wire = concat(
        {ascii(kTempLine), encodeFlashFrame(ack), ascii(kHumidityLine)});

    FlashFrameParser parser;
    const auto out = parser.consume(wire);
    ASSERT_EQ(out.frames.size(), 1U);
    EXPECT_EQ(out.frames[0], ack);
    EXPECT_EQ(out.text, std::string(kTempLine) + std::string(kHumidityLine));
}

TEST(FlashFrameParserTest, BadCrcIsDroppedAndTheNextFrameStillDecodes) {
    std::vector<std::byte> corrupted = encodeFlashFrame(FlashFrame{kAck, 4, {}});
    corrupted.back() ^= std::byte{0xFF};  // flip the CRC's high byte
    const FlashFrame next{kAck, 5, {}};

    FlashFrameParser parser;
    const auto out = parser.consume(concat({corrupted, encodeFlashFrame(next)}));
    ASSERT_EQ(out.frames.size(), 1U);
    EXPECT_EQ(out.frames[0], next);
    EXPECT_EQ(parser.falseStarts(), 1U);
}

TEST(FlashFrameParserTest, ImpossibleLenNeverSwallowsTheFramesBehindIt) {
    // A garbage LEN of 65535. Skipping LEN bytes would eat the real ACK
    // that follows. Resync from the next byte finds it at once.
    const auto garbage = bytes({0xA5, 0x00, 0x00, 0x00, 0xFF, 0xFF});
    const FlashFrame ack{kAck, 6, {}};

    FlashFrameParser parser;
    const auto out = parser.consume(concat({garbage, encodeFlashFrame(ack)}));
    ASSERT_EQ(out.frames.size(), 1U);
    EXPECT_EQ(out.frames[0], ack);
    EXPECT_EQ(parser.falseStarts(), 1U);
}

TEST(FlashFrameParserTest, StrayStartByteBeforeTextLosesOnlyItself) {
    // In "temp,..." the would-be LEN bytes are 'p' and ',', far above the
    // maximum, so the stray 0xA5 is rejected immediately and the line is
    // passed on without delay.
    FlashFrameParser parser;
    const auto out = parser.consume(concat({{kFlashStartByte}, ascii(kTempLine)}));
    EXPECT_TRUE(out.frames.empty());
    EXPECT_EQ(out.text, kTempLine);
    EXPECT_EQ(parser.falseStarts(), 1U);
}

TEST(FlashFrameParserTest, AnAckTruncatedByOneByteIsABadFrameNotAShortOne) {
    // Measured on the board, not invented. Firmware's first bank-switch build
    // answered COMMIT and reset itself immediately to switch banks, so the
    // last CRC byte was still in the shift register when the line went down:
    // it waited for the data register to free up (TXE) rather than for the
    // line to go idle (TC). The host saw seven bytes of an eight-byte ACK,
    // and an update that had actually succeeded arrived as a corrupt answer,
    // followed by a retry of an update that had already happened.
    //
    // Fixed on their side. Kept here because one byte short is exactly the
    // shape a board resetting too early makes, and the rule that matters is
    // that the frame behind it still survives.
    constexpr std::uint16_t kCommitSeq = 0x0024;
    const FlashFrame        ack{kAck, kCommitSeq, {}};

    // The bytes they captured, byte for byte, after the fix.
    EXPECT_EQ(encodeFlashFrame(ack),
              bytes({0xA5, 0x82, 0x24, 0x00, 0x00, 0x00, 0xE0, 0x8A}))
        << "the encoder no longer agrees with what the board put on the wire";

    // And what arrived before it: the same frame, one byte short.
    const auto truncated = bytes({0xA5, 0x82, 0x24, 0x00, 0x00, 0x00, 0xE0});

    FlashFrameParser parser;
    // On its own it is an unfinished frame, so the parser holds it and says
    // nothing. That is what made the host sit out its whole answer budget --
    // 4013 ms on their capture -- instead of reporting anything.
    const auto held = parser.consume(truncated);
    EXPECT_TRUE(held.frames.empty());
    EXPECT_TRUE(held.text.empty()) << "frame bytes must not leak into text";
    EXPECT_EQ(parser.falseStarts(), 0U) << "nothing is wrong with it yet";

    // Then the next real frame arrives behind it. Now the held bytes plus the
    // new start byte make up a full-length frame with the wrong CRC, which is
    // the point: it is rejected as a BAD frame, not accepted as a short one,
    // and the resync finds the good frame behind it.
    const FlashFrame next{kAck, kCommitSeq + 1, {}};
    const auto       out = parser.consume(encodeFlashFrame(next));
    ASSERT_EQ(out.frames.size(), 1U);
    EXPECT_EQ(out.frames[0], next)
        << "the truncated ACK swallowed the frame behind it";
    EXPECT_EQ(parser.falseStarts(), 1U)
        << "a frame one byte short must be counted as a bad frame";

    // The six bytes left over from the truncated frame come out as telemetry
    // text. That is the byte-by-byte resync rule doing its job, the same as
    // for a bad CRC or an impossible LEN: noise on the text stream is the
    // price of never skipping past a real frame.
    EXPECT_EQ(out.text.size(), 6U);
}

TEST(FlashFrameParserTest, UnfinishedFrameIsHeldUntilTheRestArrives) {
    const FlashFrame ack{kAck, 8, {}};
    const std::vector<std::byte> wire = encodeFlashFrame(ack);
    const auto half = static_cast<std::ptrdiff_t>(wire.size() / 2);

    FlashFrameParser parser;
    const auto first = parser.consume(
        std::vector<std::byte>(wire.begin(), wire.begin() + half));
    EXPECT_TRUE(first.frames.empty());
    EXPECT_TRUE(first.text.empty()) << "frame bytes must not leak into text";

    const auto second = parser.consume(
        std::vector<std::byte>(wire.begin() + half, wire.end()));
    ASSERT_EQ(second.frames.size(), 1U);
    EXPECT_EQ(second.frames[0], ack);
}

}  // namespace
