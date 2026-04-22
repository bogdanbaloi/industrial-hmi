// Tests for app::core::StandardExceptionHandler + safeExecute + ExceptionGuard.
//
// StandardExceptionHandler is the concrete ExceptionHandler implementation
// the rest of the app is expected to use once runtime-exception reporting
// is wired up end-to-end. Pinning its behaviour keeps the contract stable:
//   - handle() records a std::exception, counts it, and runs any matching
//     recovery handler registered by type name
//   - handleUnknown() records a non-std exception and bumps a separate counter
//   - resetStats() clears both counters
//   - safeExecute returns true on clean runs, false on exceptions, and
//     routes both std and non-std exceptions through the handler
//   - ExceptionGuard fires handleUnknown only when destroyed during stack
//     unwinding (not on normal scope exit)

#include "src/core/ExceptionHandler.h"
#include "src/core/LoggerImpl.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

using app::core::ExceptionGuard;
using app::core::ExceptionHandler;
using app::core::Logger;
using app::core::NullLogger;
using app::core::safeExecute;
using app::core::StandardExceptionHandler;

namespace {

/// Shared fixture: a NullLogger-backed handler so we don't spam stderr.
class StandardHandlerFixture : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = std::make_unique<Logger>(std::make_unique<NullLogger>());
        handler_ = std::make_unique<StandardExceptionHandler>(*logger_);
    }

    std::unique_ptr<Logger>                   logger_;
    std::unique_ptr<StandardExceptionHandler> handler_;
};

}  // namespace

TEST_F(StandardHandlerFixture, InitialCountersAreZero) {
    EXPECT_EQ(handler_->getExceptionCount(), 0u);
    EXPECT_EQ(handler_->getUnknownExceptionCount(), 0u);
}

TEST_F(StandardHandlerFixture, HandleIncrementsKnownCounterOnly) {
    const std::runtime_error e{"boom"};
    handler_->handle(e, "ctx", std::source_location::current());

    EXPECT_EQ(handler_->getExceptionCount(), 1u);
    EXPECT_EQ(handler_->getUnknownExceptionCount(), 0u);
}

TEST_F(StandardHandlerFixture, HandleUnknownIncrementsUnknownCounterOnly) {
    handler_->handleUnknown("ctx", std::source_location::current());

    EXPECT_EQ(handler_->getExceptionCount(), 0u);
    EXPECT_EQ(handler_->getUnknownExceptionCount(), 1u);
}

TEST_F(StandardHandlerFixture, ResetStatsClearsBothCounters) {
    handler_->handle(std::runtime_error{"a"}, "x",
                     std::source_location::current());
    handler_->handleUnknown("y", std::source_location::current());
    ASSERT_EQ(handler_->getExceptionCount(), 1u);
    ASSERT_EQ(handler_->getUnknownExceptionCount(), 1u);

    handler_->resetStats();
    EXPECT_EQ(handler_->getExceptionCount(), 0u);
    EXPECT_EQ(handler_->getUnknownExceptionCount(), 0u);
}

TEST_F(StandardHandlerFixture, RecoveryHandlerFiresForMatchingType) {
    int invoked = 0;
    handler_->registerRecoveryHandler(
        typeid(std::runtime_error).name(),
        [&](const std::exception&) { ++invoked; });

    handler_->handle(std::runtime_error{"boom"}, "ctx",
                     std::source_location::current());

    EXPECT_EQ(invoked, 1);
    EXPECT_EQ(handler_->getExceptionCount(), 1u);
}

TEST_F(StandardHandlerFixture, RecoveryHandlerIgnoresOtherTypes) {
    int invoked = 0;
    handler_->registerRecoveryHandler(
        typeid(std::logic_error).name(),
        [&](const std::exception&) { ++invoked; });

    // runtime_error -> no match -> no invocation
    handler_->handle(std::runtime_error{"boom"}, "ctx",
                     std::source_location::current());

    EXPECT_EQ(invoked, 0);
}

TEST_F(StandardHandlerFixture, RecoveryHandlerThrowingDoesNotCrash) {
    // A recovery handler that itself throws gets its error logged as
    // "Recovery failed: ..." — swallowed by StandardExceptionHandler so
    // the original exception path completes normally.
    handler_->registerRecoveryHandler(
        typeid(std::runtime_error).name(),
        [](const std::exception&) {
            throw std::runtime_error{"recovery itself blew up"};
        });

    EXPECT_NO_THROW(handler_->handle(
        std::runtime_error{"primary"}, "ctx",
        std::source_location::current()));
    EXPECT_EQ(handler_->getExceptionCount(), 1u);
}

// ---------------------------------------------------------------------------
// safeExecute
// ---------------------------------------------------------------------------

TEST_F(StandardHandlerFixture, SafeExecuteReturnsTrueOnCleanRun) {
    bool ran = false;
    const bool ok = safeExecute(*handler_, [&] { ran = true; }, "ok");
    EXPECT_TRUE(ok);
    EXPECT_TRUE(ran);
    EXPECT_EQ(handler_->getExceptionCount(), 0u);
    EXPECT_EQ(handler_->getUnknownExceptionCount(), 0u);
}

TEST_F(StandardHandlerFixture, SafeExecuteCatchesStdException) {
    const bool ok = safeExecute(*handler_,
        [] { throw std::runtime_error{"x"}; }, "std");

    EXPECT_FALSE(ok);
    EXPECT_EQ(handler_->getExceptionCount(), 1u);
    EXPECT_EQ(handler_->getUnknownExceptionCount(), 0u);
}

TEST_F(StandardHandlerFixture, SafeExecuteCatchesNonStdException) {
    const bool ok = safeExecute(*handler_,
        [] { throw 42; }, "int");

    EXPECT_FALSE(ok);
    EXPECT_EQ(handler_->getExceptionCount(), 0u);
    EXPECT_EQ(handler_->getUnknownExceptionCount(), 1u);
}

// ---------------------------------------------------------------------------
// ExceptionGuard
// ---------------------------------------------------------------------------

TEST_F(StandardHandlerFixture, ExceptionGuardDoesNothingOnNormalExit) {
    {
        ExceptionGuard g{*handler_, "normal"};
        (void)g;
    }
    EXPECT_EQ(handler_->getUnknownExceptionCount(), 0u);
}

TEST_F(StandardHandlerFixture, ExceptionGuardFiresOnStackUnwind) {
    try {
        ExceptionGuard g{*handler_, "uncaught"};
        (void)g;
        throw std::runtime_error{"escape"};
    } catch (...) {
        // swallow so the test process survives
    }
    EXPECT_EQ(handler_->getUnknownExceptionCount(), 1u);
}
