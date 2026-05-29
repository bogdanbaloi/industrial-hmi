// Tests for app::core::formatIso8601Local -- a pure utility (no
// dedicated requirement; it backs the audit + history timestamp
// columns). No OFT coverage tag on purpose: mapping it to an
// unrelated REQ would be dishonest, and OFT only forbids orphaned
// tags / uncovered requirements, not untagged utility tests.
//
// formatIso8601Local converts a stored UTC ISO-8601 stamp into a
// friendlier local-time string. The conversion is timezone-dependent,
// so the fixture pins TZ=UTC -- then local == UTC and a known input has
// a deterministic output. The fallback + shape cases are timezone-
// independent regardless.

#include "src/core/TimeFormat.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <regex>
#include <string>

namespace {

class TimeFormatTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Force UTC so UTC-in == local-out and exact assertions are
        // deterministic across CI runners. setenv/tzset are available
        // on the Linux + MSYS2/CLANG64 build environments this project
        // targets. Each test binary is its own process, so this does
        // not leak into other suites.
        ::setenv("TZ", "UTC", /*overwrite=*/1);
        ::tzset();
    }
};

TEST_F(TimeFormatTest, ConvertsValidUtcStampToLocalDisplay) {
    // TZ=UTC -> the local rendering equals the UTC wall-clock time,
    // reformatted from "...T...Z" to "... ..." (space, no trailing Z).
    EXPECT_EQ(app::core::formatIso8601Local("2024-04-09T14:30:22Z"),
              "2024-04-09 14:30:22");
}

TEST_F(TimeFormatTest, OutputMatchesExpectedShape) {
    const std::string out =
        app::core::formatIso8601Local("2026-12-31T23:59:59Z");
    // "YYYY-MM-DD HH:MM:SS" regardless of timezone.
    const std::regex shape(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$)");
    EXPECT_TRUE(std::regex_match(out, shape)) << "got: " << out;
}

TEST_F(TimeFormatTest, MalformedInputFallsBackToRaw) {
    // Corrupted / unparseable rows must still render something visible
    // (the raw input verbatim) rather than an empty / crashing cell.
    // Note: out-of-range-but-well-formed fields (e.g. month 13) are NOT
    // a fallback case -- timegm/mktime normalises them into a valid
    // date, so we only assert on genuinely unparseable inputs here:
    //   * non-numeric garbage,
    //   * empty string,
    //   * too short to hold the fixed 20-char "YYYY-MM-DDTHH:MM:SSZ".
    EXPECT_EQ(app::core::formatIso8601Local("not-a-timestamp"),
              "not-a-timestamp");
    EXPECT_EQ(app::core::formatIso8601Local(""), "");
    EXPECT_EQ(app::core::formatIso8601Local("2024-04-09"), "2024-04-09");
}

}  // namespace
