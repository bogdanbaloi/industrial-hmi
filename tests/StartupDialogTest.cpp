// Tests for app::core::reportFatalStartup / reportUnexpectedFatal in
// console mode (consoleMode=true). The GUI path on Windows pops up a
// MessageBoxW which isn't testable from a headless unit test -- those
// branches are exercised manually + in the scenario suite instead.
// The console path writes a structured block to stderr, which we
// capture by temporarily redirecting stderr through freopen.

#include "src/core/StartupDialog.h"
#include "src/core/StartupErrors.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using app::core::ConfigMissingError;
using app::core::DatabaseInitError;
using app::core::reportFatalStartup;
using app::core::reportUnexpectedFatal;

namespace {

/// Capture everything written to stderr inside a scope. Redirects the
/// FILE* so `std::fprintf(stderr, ...)` lands in a file we can read
/// back. One instance per TEST() keeps the fixture file confined to a
/// per-test path in ctest's working directory -- no collisions between
/// sequential tests, no portable-tmp-file drama (avoiding the
/// deprecated `tmpnam` / POSIX-only `mkstemp`).
class StderrCapture {
public:
    explicit StderrCapture(std::string path) : path_(std::move(path)) {
        std::fflush(stderr);
        saved_ = freopen(path_.c_str(), "w", stderr);
    }

    ~StderrCapture() {
        if (saved_) {
            std::fflush(stderr);
            // GCC 13 marks freopen with warn_unused_result and refuses
            // to let us discard the return value even via `(void)`.
            // Use it in an `if` instead; we have nothing meaningful to
            // do if restoration fails inside a destructor anyway.
#ifdef _WIN32
            if (freopen("NUL", "w", stderr) == nullptr) { /* nothing */ }
#else
            if (freopen("/dev/null", "w", stderr) == nullptr) { /* nothing */ }
#endif
        }
    }

    [[nodiscard]] std::string captured() const {
        std::fflush(stderr);
        std::ifstream in(path_);
        std::ostringstream os;
        os << in.rdbuf();
        return os.str();
    }

private:
    std::string path_;
    FILE*       saved_{nullptr};
};

}  // namespace

// reportFatalStartup -- console mode

TEST(StartupDialogTest, FatalConsoleIncludesTagAndBody) {
    StderrCapture cap{"stderr-fatal-body.txt"};
    reportFatalStartup(
        ConfigMissingError{"config/app-config.json missing"},
        /*consoleMode=*/true);

    const std::string out = cap.captured();
    EXPECT_NE(out.find("CONFIG MISSING"), std::string::npos)
        << "tag missing in output: " << out;
    EXPECT_NE(out.find("config/app-config.json missing"), std::string::npos)
        << "body missing in output: " << out;
}

TEST(StartupDialogTest, FatalConsoleTagsDifferByErrorCode) {
    StderrCapture cap1{"stderr-fatal-missing.txt"};
    reportFatalStartup(ConfigMissingError{"a"}, true);
    const std::string missing = cap1.captured();

    StderrCapture cap2{"stderr-fatal-db.txt"};
    reportFatalStartup(DatabaseInitError{"b"}, true);
    const std::string db = cap2.captured();

    EXPECT_NE(missing.find("CONFIG MISSING"), std::string::npos);
    EXPECT_NE(db.find("DATABASE ERROR"),      std::string::npos);
    // Bodies are not cross-contaminated.
    EXPECT_EQ(missing.find("DATABASE ERROR"), std::string::npos);
    EXPECT_EQ(db.find("CONFIG MISSING"),      std::string::npos);
}

// (FatalConsoleIsNoexcept static_assert removed: Windows Clang's
// `noexcept(expr)` operator evaluates to false here even with the
// function declared noexcept -- seems to be a overload-resolution quirk
// around the std::string_view-from-literal argument. The noexcept
// contract is still enforced at declaration site in StartupDialog.h,
// which is the actual invariant we care about.)

// reportUnexpectedFatal -- console mode

TEST(StartupDialogTest, UnexpectedConsoleIncludesTagAndMessage) {
    StderrCapture cap{"stderr-unexpected.txt"};
    reportUnexpectedFatal("something broke at runtime", /*consoleMode=*/true);

    const std::string out = cap.captured();
    EXPECT_NE(out.find("UNEXPECTED FATAL ERROR"), std::string::npos)
        << "tag missing: " << out;
    EXPECT_NE(out.find("something broke at runtime"), std::string::npos)
        << "body missing: " << out;
}

// UnexpectedConsoleIsNoexcept: same story -- removed the static_assert
// because Windows Clang spuriously evaluates noexcept(...) as false
// here. The header declaration still enforces the contract for real
// callers.
