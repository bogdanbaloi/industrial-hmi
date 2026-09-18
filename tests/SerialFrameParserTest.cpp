// [utest->req~integration-009~1]
// Covers REQ-INTEGRATION-009 (serial telemetry ingest) at the framing
// layer: turning the raw serial BYTE STREAM into whole `sensorId,value`
// readings.
//
// Pure-logic test with no serial port and no boost::asio: SerialFrameParser
// performs no I/O, so a test drives it by feeding bytes straight to
// consume(), in whatever chunk boundaries it wants. The headline case is a
// single reading split across two consume() calls, which proves the parser
// reassembles a frame that arrived in pieces.

#include "src/integration/SerialFrameParser.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using app::integration::SerialFrameParser;
using app::integration::SerialReading;

TEST(SerialFrameParserTest, EmptyInputYieldsNoReadings) {
    SerialFrameParser parser;
    EXPECT_TRUE(parser.consume("").empty());
}

TEST(SerialFrameParserTest, SingleCompleteFrameParses) {
    SerialFrameParser parser;
    const auto readings = parser.consume("temp,23.5\n");
    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings[0].sensorId, "temp");
    EXPECT_EQ(readings[0].value, "23.5");
}

TEST(SerialFrameParserTest, MultipleFramesInOneChunk) {
    SerialFrameParser parser;
    const auto readings = parser.consume("temp,23\nhumidity,60\n");
    ASSERT_EQ(readings.size(), 2U);
    EXPECT_EQ(readings[0].sensorId, "temp");
    EXPECT_EQ(readings[0].value, "23");
    EXPECT_EQ(readings[1].sensorId, "humidity");
    EXPECT_EQ(readings[1].value, "60");
}

// Headline case: one reading arrives split across two consume() calls. The
// first call sees no newline and must return nothing while holding the
// partial; the second call completes and emits it.
TEST(SerialFrameParserTest, FrameSplitAcrossTwoConsumesIsReassembled) {
    SerialFrameParser parser;
    EXPECT_TRUE(parser.consume("temp,23").empty());
    const auto readings = parser.consume(".5\n");
    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings[0].sensorId, "temp");
    EXPECT_EQ(readings[0].value, "23.5");
}

// A complete frame followed by the start of the next one: the complete one
// is emitted now, the partial is held for the next chunk.
TEST(SerialFrameParserTest, PartialAfterCompleteFrameIsHeld) {
    SerialFrameParser parser;
    const auto first = parser.consume("temp,23.5\nhum");
    ASSERT_EQ(first.size(), 1U);
    EXPECT_EQ(first[0].sensorId, "temp");

    const auto second = parser.consume("idity,60\n");
    ASSERT_EQ(second.size(), 1U);
    EXPECT_EQ(second[0].sensorId, "humidity");
    EXPECT_EQ(second[0].value, "60");
}

TEST(SerialFrameParserTest, EmptyLinesAreSkipped) {
    SerialFrameParser parser;
    const auto readings = parser.consume("\n\ntemp,23\n\n");
    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings[0].sensorId, "temp");
}

TEST(SerialFrameParserTest, LineWithoutFieldDelimiterIsSkipped) {
    SerialFrameParser parser;
    EXPECT_TRUE(parser.consume("noseparator\n").empty());
}

TEST(SerialFrameParserTest, LineWithExtraFieldDelimiterIsSkipped) {
    SerialFrameParser parser;
    EXPECT_TRUE(parser.consume("a,b,c\n").empty());
}

TEST(SerialFrameParserTest, EmptyFieldsAreSkipped) {
    SerialFrameParser parser;
    EXPECT_TRUE(parser.consume(",23\n").empty());    // empty sensor id
    EXPECT_TRUE(parser.consume("temp,\n").empty());  // empty value
}

TEST(SerialFrameParserTest, CrlfLineEndingIsTolerated) {
    SerialFrameParser parser;
    const auto readings = parser.consume("temp,23.5\r\n");
    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings[0].sensorId, "temp");
    EXPECT_EQ(readings[0].value, "23.5");
}

// A malformed line does not poison the frames around it.
TEST(SerialFrameParserTest, MalformedLineDoesNotDropTheValidOne) {
    SerialFrameParser parser;
    const auto readings = parser.consume("garbage\ntemp,23\n");
    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings[0].sensorId, "temp");
    EXPECT_EQ(readings[0].value, "23");
}

// A runaway partial (bytes that never terminate) is dropped by the buffer
// guard, and the parser keeps working for the next real frame.
TEST(SerialFrameParserTest, RunawayPartialIsDroppedThenRecovers) {
    SerialFrameParser parser;
    const std::string runaway(5000, 'x');  // exceeds the buffer guard, no newline
    EXPECT_TRUE(parser.consume(runaway).empty());

    const auto readings = parser.consume("temp,23\n");
    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings[0].sensorId, "temp");
    EXPECT_EQ(readings[0].value, "23");
}

// Contract pin (ADR-0029): the reference device reports `temp` in degrees
// Celsius, so a sub-zero reading carries a leading minus. The parser keeps a
// value as opaque text and never parses a number, which is exactly why this
// works -- this case exists so a future "validate the value" change cannot
// silently break the real device.
TEST(SerialFrameParserTest, NegativeTemperatureValueParses) {
    SerialFrameParser parser;
    const auto readings = parser.consume("temp,-3.5\n");
    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings[0].sensorId, "temp");
    EXPECT_EQ(readings[0].value, "-3.5");
}

// Contract pin (ADR-0029): the device drops the `temp` frame entirely when it
// has no valid reading, so one button press can arrive as a single frame. The
// parser is line-based and holds no cross-frame state, so nothing pairs the
// two -- this case exists so a future "expect both frames" assumption cannot
// creep in unnoticed.
TEST(SerialFrameParserTest, PressWithoutTemperatureYieldsOnlyTheStateFrame) {
    SerialFrameParser parser;
    const auto readings = parser.consume("equipment/3/state,on\n");
    ASSERT_EQ(readings.size(), 1U);
    EXPECT_EQ(readings[0].sensorId, "equipment/3/state");
    EXPECT_EQ(readings[0].value, "on");
}

}  // namespace
