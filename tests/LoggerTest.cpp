// Tests for app::core::Logger, ConsoleLogger, and helper functions.
// Uses a custom CapturingLogger to intercept log output without I/O.

#include "src/core/LoggerBase.h"
#include "src/core/LoggerImpl.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using app::core::LogLevel;
using app::core::Logger;
using app::core::LoggerBase;
using app::core::ConsoleLogger;
using app::core::CallbackLogger;

namespace {

// Minimal LoggerBase that captures messages for assertion.
class CapturingLogger : public LoggerBase {
public:
    struct Entry {
        LogLevel level;
        std::string message;
    };

    explicit CapturingLogger(LogLevel minLevel = LogLevel::DEBUG)
        : minLevel_(minLevel) {}

    void log(LogLevel level, std::string_view message,
             const std::source_location&) override {
        entries.push_back({level, std::string(message)});
    }

    bool isEnabled(LogLevel level) const override {
        return level >= minLevel_;
    }

    void setLevel(LogLevel level) override { minLevel_ = level; }

    std::vector<Entry> entries;

private:
    LogLevel minLevel_;
};

}  // namespace

// ============================================================================
// levelToString
// ============================================================================

TEST(LoggerHelpersTest, LevelToStringMapsAllLevels) {
    EXPECT_STREQ(app::core::levelToString(LogLevel::DEBUG),    "DEBUG");
    EXPECT_STREQ(app::core::levelToString(LogLevel::INFO),     "INFO ");
    EXPECT_STREQ(app::core::levelToString(LogLevel::WARN),     "WARN ");
    EXPECT_STREQ(app::core::levelToString(LogLevel::ERROR),    "ERROR");
    EXPECT_STREQ(app::core::levelToString(LogLevel::CRITICAL), "CRIT ");
}

// ============================================================================
// formatTimestamp
// ============================================================================

TEST(LoggerHelpersTest, FormatTimestampProducesNonEmptyString) {
    auto ts = app::core::formatTimestamp();
    EXPECT_FALSE(ts.empty());
    // Should contain date separator and time separator
    EXPECT_NE(ts.find('-'), std::string::npos);
    EXPECT_NE(ts.find(':'), std::string::npos);
}

// ============================================================================
// parseLogLevel
// ============================================================================

TEST(LoggerHelpersTest, ParseLogLevelMapsStrings) {
    EXPECT_EQ(app::core::parseLogLevel("DEBUG"), LogLevel::DEBUG);
    EXPECT_EQ(app::core::parseLogLevel("INFO"), LogLevel::INFO);
    EXPECT_EQ(app::core::parseLogLevel("WARN"), LogLevel::WARN);
    EXPECT_EQ(app::core::parseLogLevel("ERROR"), LogLevel::ERROR);
    EXPECT_EQ(app::core::parseLogLevel("CRITICAL"), LogLevel::CRITICAL);
}

TEST(LoggerHelpersTest, ParseLogLevelDefaultsToInfoForUnknown) {
    EXPECT_EQ(app::core::parseLogLevel("GARBAGE"), LogLevel::INFO);
    EXPECT_EQ(app::core::parseLogLevel(""), LogLevel::INFO);
}

// ============================================================================
// Logger facade with CapturingLogger
// ============================================================================

TEST(LoggerTest, InfoLogsWhenLevelIsDebug) {
    auto capturing = std::make_unique<CapturingLogger>(LogLevel::DEBUG);
    auto* raw = capturing.get();
    Logger logger(std::move(capturing));

    logger.info("hello {}", 42);

    ASSERT_EQ(raw->entries.size(), 1u);
    EXPECT_EQ(raw->entries[0].level, LogLevel::INFO);
    EXPECT_EQ(raw->entries[0].message, "hello 42");
}

TEST(LoggerTest, DebugSuppressedWhenLevelIsInfo) {
    auto capturing = std::make_unique<CapturingLogger>(LogLevel::INFO);
    auto* raw = capturing.get();
    Logger logger(std::move(capturing));

    logger.debug("should not appear");

    EXPECT_TRUE(raw->entries.empty());
}

TEST(LoggerTest, ErrorLogsWhenLevelIsWarn) {
    auto capturing = std::make_unique<CapturingLogger>(LogLevel::WARN);
    auto* raw = capturing.get();
    Logger logger(std::move(capturing));

    logger.error("oops: {}", "disk full");

    ASSERT_EQ(raw->entries.size(), 1u);
    EXPECT_EQ(raw->entries[0].level, LogLevel::ERROR);
    EXPECT_EQ(raw->entries[0].message, "oops: disk full");
}

TEST(LoggerTest, SetLevelChangesFiltering) {
    auto capturing = std::make_unique<CapturingLogger>(LogLevel::ERROR);
    auto* raw = capturing.get();
    Logger logger(std::move(capturing));

    logger.info("suppressed");
    EXPECT_TRUE(raw->entries.empty());

    logger.setLevel(LogLevel::DEBUG);
    logger.info("now visible");
    ASSERT_EQ(raw->entries.size(), 1u);
    EXPECT_EQ(raw->entries[0].message, "now visible");
}

TEST(LoggerTest, CriticalAlwaysLogsAtAnyLevel) {
    auto capturing = std::make_unique<CapturingLogger>(LogLevel::CRITICAL);
    auto* raw = capturing.get();
    Logger logger(std::move(capturing));

    logger.critical("fatal: {}", "core dump");

    ASSERT_EQ(raw->entries.size(), 1u);
    EXPECT_EQ(raw->entries[0].level, LogLevel::CRITICAL);
}

TEST(LoggerTest, WarnLogsCorrectly) {
    auto capturing = std::make_unique<CapturingLogger>(LogLevel::DEBUG);
    auto* raw = capturing.get();
    Logger logger(std::move(capturing));

    logger.warn("low disk: {}%", 5);

    ASSERT_EQ(raw->entries.size(), 1u);
    EXPECT_EQ(raw->entries[0].level, LogLevel::WARN);
}

// ============================================================================
// ConsoleLogger (just verifying it doesn't crash)
// ============================================================================

TEST(ConsoleLoggerTest, LogDoesNotCrash) {
    ConsoleLogger cl(LogLevel::DEBUG);
    EXPECT_TRUE(cl.isEnabled(LogLevel::DEBUG));
    EXPECT_NO_THROW(cl.log(LogLevel::INFO, "test message",
                           std::source_location::current()));
    EXPECT_NO_THROW(cl.flush());
}

TEST(ConsoleLoggerTest, IsEnabledRespectsMinLevel) {
    ConsoleLogger cl(LogLevel::WARN);
    EXPECT_FALSE(cl.isEnabled(LogLevel::DEBUG));
    EXPECT_FALSE(cl.isEnabled(LogLevel::INFO));
    EXPECT_TRUE(cl.isEnabled(LogLevel::WARN));
    EXPECT_TRUE(cl.isEnabled(LogLevel::ERROR));
}

// ============================================================================
// CallbackLogger
// ============================================================================

TEST(CallbackLoggerTest, CallbackReceivesFormattedMessage) {
    std::string captured;
    CallbackLogger cl([&](const std::string& msg) { captured = msg; },
                      LogLevel::DEBUG);

    cl.log(LogLevel::INFO, "test payload", std::source_location::current());

    EXPECT_FALSE(captured.empty());
    EXPECT_NE(captured.find("test payload"), std::string::npos);
}
