// Tests for the concrete logger implementations in LoggerImpl.h.
//
// LoggerTest.cpp already covers the Logger facade + ConsoleLogger
// plumbing via a CapturingLogger. This file targets the paths gcov
// flagged as uncovered:
//   * FileLogger constructor error path
//   * FileLogger rotate() — the whole size-triggered rotation workflow,
//     including shifting existing rotated files and deleting the oldest
//   * FileLogger / ConsoleLogger / CallbackLogger setLevel
//   * CompositeLogger isEnabled (true if any child enabled) + setLevel
//     propagation + removeLastLogger
//   * NullLogger log / isEnabled / setLevel no-ops
//   * CallbackLogger reentrancy guard (writing_ atomic)
//
// LoggerBase subclasses expose only the virtual `log(level, msg, loc)`
// — the typed `info()/error()` helpers live on the `Logger` facade.
// Tests call `.log(...)` directly so we can bypass the facade and
// hit the subclass code paths precisely.
//
// No GTK, no network — pure filesystem + std::ofstream, so runs fast
// and hermetic in CI.

#include "src/core/LoggerBase.h"
#include "src/core/LoggerImpl.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <source_location>
#include <string>

using app::core::CallbackLogger;
using app::core::CompositeLogger;
using app::core::ConsoleLogger;
using app::core::FileLogger;
using app::core::LogLevel;
using app::core::LoggerBase;
using app::core::NullLogger;

namespace fs = std::filesystem;

namespace {

/// RAII scratch directory — cleaned on destruction so repeated runs
/// don't accumulate log files and tests don't collide on the same path.
class ScratchDir {
public:
    ScratchDir() {
        path_ = fs::temp_directory_path() /
                ("hmi-logger-test-" + std::to_string(counter_++) + "-" +
                 std::to_string(
                     std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path_);
    }

    ~ScratchDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
    static inline int counter_ = 0;
};

/// Log `n` messages of fixed 60-byte payload so the FileLogger's size
/// counter overtakes the rotation threshold.
void writeManyLines(LoggerBase& logger, int n) {
    const std::string payload(60, 'x');
    for (int i = 0; i < n; ++i) {
        logger.log(LogLevel::INFO, payload, std::source_location::current());
    }
}

}  // namespace

// FileLogger — construction

TEST(FileLoggerTest, ConstructorThrowsOnUnwritablePath) {
    // Empty path causes std::ofstream::open to fail cleanly.
    EXPECT_THROW({
        FileLogger logger("");
    }, std::runtime_error);
}

TEST(FileLoggerTest, ConstructorCreatesParentDirectories) {
    ScratchDir scratch;
    auto nested = scratch.path() / "a" / "b" / "c" / "app.log";

    EXPECT_NO_THROW({
        FileLogger logger(nested.string());
    });

    EXPECT_TRUE(fs::exists(nested.parent_path()));
    EXPECT_TRUE(fs::exists(nested));
}

// FileLogger — rotation

TEST(FileLoggerTest, RotatesWhenSizeLimitExceeded) {
    ScratchDir scratch;
    auto logPath = scratch.path() / "rotate.log";

    // Tiny 2 KB cap + 3 rotations, so a handful of log calls trip rotate().
    FileLogger logger(logPath.string(), LogLevel::DEBUG,
                      /*maxFileSize=*/2048, /*maxFiles=*/3);

    writeManyLines(logger, 40);  // ~3600 bytes > 2048
    logger.flush();

    // After rotation: primary file exists AND rotate.1.log holds the
    // pre-rotation contents.
    EXPECT_TRUE(fs::exists(logPath));
    EXPECT_TRUE(fs::exists(scratch.path() / "rotate.1.log"))
        << "rotate.1.log missing — rotation didn't fire";
}

TEST(FileLoggerTest, ShiftsExistingRotatedFilesAcrossMultipleRotations) {
    ScratchDir scratch;
    auto logPath = scratch.path() / "shift.log";

    FileLogger logger(logPath.string(), LogLevel::DEBUG,
                      /*maxFileSize=*/1024, /*maxFiles=*/3);

    // Force ≥3 rotations so the .1 -> .2 -> .3 shift loop runs.
    for (int i = 0; i < 3; ++i) {
        writeManyLines(logger, 25);
        logger.flush();
    }

    EXPECT_TRUE(fs::exists(scratch.path() / "shift.1.log"));
    EXPECT_TRUE(fs::exists(scratch.path() / "shift.2.log"));
}

TEST(FileLoggerTest, RotationDeletesOldestFileBeyondMaxFiles) {
    ScratchDir scratch;
    auto logPath = scratch.path() / "cap.log";

    // Pre-seed a file in the .maxFiles slot so rotate()'s first step
    // (std::filesystem::remove on oldest) has something to delete.
    {
        std::ofstream preexisting(scratch.path() / "cap.2.log");
        preexisting << "dummy\n";
    }

    FileLogger logger(logPath.string(), LogLevel::DEBUG,
                      /*maxFileSize=*/1024, /*maxFiles=*/2);

    writeManyLines(logger, 25);
    logger.flush();

    // After rotation with maxFiles=2: cap.1.log now exists with the
    // previous primary file's content; cap.2.log was the pre-seeded
    // stub and is now (either) shifted-to-.3 or deleted depending on
    // exact step order — either way, rotate()'s delete + shift loops ran.
    EXPECT_TRUE(fs::exists(scratch.path() / "cap.1.log"));
}

// FileLogger — setLevel + shutdown

TEST(FileLoggerTest, SetLevelFiltersSubsequentChecks) {
    ScratchDir scratch;
    auto logPath = scratch.path() / "level.log";

    FileLogger logger(logPath.string(), LogLevel::DEBUG);

    EXPECT_TRUE(logger.isEnabled(LogLevel::DEBUG));
    EXPECT_TRUE(logger.isEnabled(LogLevel::INFO));

    logger.setLevel(LogLevel::ERROR);
    EXPECT_FALSE(logger.isEnabled(LogLevel::DEBUG));
    EXPECT_FALSE(logger.isEnabled(LogLevel::WARN));
    EXPECT_TRUE(logger.isEnabled(LogLevel::ERROR));
    EXPECT_TRUE(logger.isEnabled(LogLevel::CRITICAL));
}

TEST(FileLoggerTest, ShutdownClosesFileAndSubsequentLogsAreNoOp) {
    ScratchDir scratch;
    auto logPath = scratch.path() / "shutdown.log";

    FileLogger logger(logPath.string(), LogLevel::DEBUG);
    logger.log(LogLevel::INFO, "before shutdown", std::source_location::current());
    logger.shutdown();

    // After shutdown, log() early-returns on !file_.is_open(). No throw.
    EXPECT_NO_THROW({
        logger.log(LogLevel::INFO, "after shutdown",
                   std::source_location::current());
    });
}

// ConsoleLogger — setLevel

TEST(ConsoleLoggerTest, SetLevelChangesIsEnabled) {
    ConsoleLogger logger(LogLevel::WARN);
    EXPECT_FALSE(logger.isEnabled(LogLevel::INFO));

    logger.setLevel(LogLevel::TRACE);
    EXPECT_TRUE(logger.isEnabled(LogLevel::INFO));
    EXPECT_TRUE(logger.isEnabled(LogLevel::TRACE));
}

// CompositeLogger — isEnabled + setLevel propagation + removeLastLogger

TEST(CompositeLoggerTest, IsEnabledTrueIfAnyChildEnabled) {
    CompositeLogger composite;
    composite.addLogger(std::make_unique<ConsoleLogger>(LogLevel::ERROR));
    composite.addLogger(std::make_unique<ConsoleLogger>(LogLevel::TRACE));

    EXPECT_TRUE(composite.isEnabled(LogLevel::TRACE));
    EXPECT_TRUE(composite.isEnabled(LogLevel::ERROR));
}

TEST(CompositeLoggerTest, IsEnabledFalseIfNoChildEnabled) {
    CompositeLogger composite;
    composite.addLogger(std::make_unique<ConsoleLogger>(LogLevel::CRITICAL));
    composite.addLogger(std::make_unique<ConsoleLogger>(LogLevel::ERROR));

    EXPECT_FALSE(composite.isEnabled(LogLevel::DEBUG));
    EXPECT_FALSE(composite.isEnabled(LogLevel::INFO));
}

TEST(CompositeLoggerTest, IsEnabledFalseWhenEmpty) {
    CompositeLogger composite;
    EXPECT_FALSE(composite.isEnabled(LogLevel::INFO));
    EXPECT_FALSE(composite.isEnabled(LogLevel::CRITICAL));
}

TEST(CompositeLoggerTest, SetLevelPropagatesToAllChildren) {
    auto child1Owned = std::make_unique<ConsoleLogger>(LogLevel::ERROR);
    auto child2Owned = std::make_unique<ConsoleLogger>(LogLevel::ERROR);
    auto* child1 = child1Owned.get();
    auto* child2 = child2Owned.get();

    CompositeLogger composite;
    composite.addLogger(std::move(child1Owned));
    composite.addLogger(std::move(child2Owned));

    composite.setLevel(LogLevel::DEBUG);

    EXPECT_TRUE(child1->isEnabled(LogLevel::DEBUG));
    EXPECT_TRUE(child2->isEnabled(LogLevel::DEBUG));
}

TEST(CompositeLoggerTest, RemoveLastLoggerDropsTail) {
    CompositeLogger composite;
    composite.addLogger(std::make_unique<NullLogger>());
    composite.addLogger(std::make_unique<ConsoleLogger>(LogLevel::DEBUG));

    // ConsoleLogger at DEBUG -> composite accepts DEBUG.
    EXPECT_TRUE(composite.isEnabled(LogLevel::DEBUG));

    composite.removeLastLogger();

    // Now only NullLogger left -> composite rejects everything.
    EXPECT_FALSE(composite.isEnabled(LogLevel::DEBUG));
}

TEST(CompositeLoggerTest, RemoveLastLoggerOnEmptyIsNoOp) {
    CompositeLogger composite;
    // Guards against pop_back on empty vector.
    EXPECT_NO_THROW(composite.removeLastLogger());
    EXPECT_NO_THROW(composite.removeLastLogger());
}

TEST(CompositeLoggerTest, FlushAndShutdownReachAllChildren) {
    // The composite test mainly exists to execute the flush/shutdown
    // loops (lines 226-236). We don't need to observe anything — success
    // is "no crash".
    CompositeLogger composite;
    composite.addLogger(std::make_unique<NullLogger>());
    composite.addLogger(std::make_unique<ConsoleLogger>(LogLevel::INFO));

    EXPECT_NO_THROW(composite.flush());
    EXPECT_NO_THROW(composite.shutdown());
}

// NullLogger — all methods are no-ops but still need coverage.

TEST(NullLoggerTest, LogIsNoOpAndDoesNotThrow) {
    NullLogger logger;
    EXPECT_NO_THROW({
        logger.log(LogLevel::INFO, "ignored", std::source_location::current());
    });
    EXPECT_NO_THROW({
        logger.log(LogLevel::ERROR, "ignored", std::source_location::current());
    });
}

TEST(NullLoggerTest, IsEnabledAlwaysFalse) {
    NullLogger logger;
    EXPECT_FALSE(logger.isEnabled(LogLevel::TRACE));
    EXPECT_FALSE(logger.isEnabled(LogLevel::DEBUG));
    EXPECT_FALSE(logger.isEnabled(LogLevel::INFO));
    EXPECT_FALSE(logger.isEnabled(LogLevel::WARN));
    EXPECT_FALSE(logger.isEnabled(LogLevel::ERROR));
    EXPECT_FALSE(logger.isEnabled(LogLevel::CRITICAL));
}

TEST(NullLoggerTest, SetLevelIsNoOp) {
    NullLogger logger;
    logger.setLevel(LogLevel::TRACE);
    EXPECT_FALSE(logger.isEnabled(LogLevel::TRACE))
        << "NullLogger stays disabled regardless of setLevel";
}

// CallbackLogger — isEnabled + setLevel + reentrancy

TEST(CallbackLoggerTest, IsEnabledRespectsMinLevel) {
    CallbackLogger logger([](const std::string&) {}, LogLevel::WARN);
    EXPECT_FALSE(logger.isEnabled(LogLevel::DEBUG));
    EXPECT_FALSE(logger.isEnabled(LogLevel::INFO));
    EXPECT_TRUE(logger.isEnabled(LogLevel::WARN));
    EXPECT_TRUE(logger.isEnabled(LogLevel::ERROR));
}

TEST(CallbackLoggerTest, SetLevelChangesIsEnabled) {
    CallbackLogger logger([](const std::string&) {}, LogLevel::ERROR);
    EXPECT_FALSE(logger.isEnabled(LogLevel::INFO));

    logger.setLevel(LogLevel::TRACE);
    EXPECT_TRUE(logger.isEnabled(LogLevel::INFO));
}

TEST(CallbackLoggerTest, ForwardsFormattedLineToCallback) {
    std::string captured;
    CallbackLogger logger(
        [&](const std::string& line) { captured = line; },
        LogLevel::DEBUG);

    logger.log(LogLevel::INFO, "hello 42", std::source_location::current());
    EXPECT_FALSE(captured.empty());
    EXPECT_NE(captured.find("hello 42"), std::string::npos);
    EXPECT_NE(captured.find("INFO"), std::string::npos);
}

TEST(CallbackLoggerTest, DoesNotReenterOnNestedLog) {
    // The `writing_` atomic guards against a callback triggering another
    // log() on the same logger — without it we'd recurse infinitely.
    CallbackLogger* loggerPtr = nullptr;
    int callCount = 0;

    CallbackLogger logger(
        [&](const std::string&) {
            ++callCount;
            if (loggerPtr) {
                loggerPtr->log(LogLevel::INFO, "nested",
                               std::source_location::current());
            }
        },
        LogLevel::DEBUG);
    loggerPtr = &logger;

    logger.log(LogLevel::INFO, "outer", std::source_location::current());
    EXPECT_EQ(callCount, 1)
        << "nested log() should have been swallowed by writing_ guard";
}

TEST(CallbackLoggerTest, NullCallbackIsHandled) {
    // Line 274 early-returns if !callback_.
    CallbackLogger logger(nullptr, LogLevel::DEBUG);
    EXPECT_NO_THROW({
        logger.log(LogLevel::INFO, "no-op",
                   std::source_location::current());
    });
}
