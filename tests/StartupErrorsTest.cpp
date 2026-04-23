// Tests for app::core::StartupErrors — the typed exception hierarchy
// thrown from Bootstrap / Application on unrecoverable startup failures.
//
// These are tiny value types (a code + a what-string), so the tests
// just pin down:
//   - `toTag` returns a distinct, stable tag per enum value
//   - each concrete subclass carries the right `StartupErrorCode`
//   - the message round-trips through `what()` unchanged
//   - the hierarchy is catchable as `CriticalStartupError` and as
//     `std::exception` (both matter: main.cpp catches the former,
//     defensive code elsewhere may catch the latter).

#include "src/core/StartupErrors.h"

#include <gtest/gtest.h>

#include <exception>
#include <string>
#include <string_view>

using app::core::ConfigCorruptError;
using app::core::ConfigMissingError;
using app::core::CriticalStartupError;
using app::core::DatabaseInitError;
using app::core::LoggerBootstrapError;
using app::core::StartupErrorCode;
using app::core::toTag;

// toTag()

TEST(StartupErrorsTest, ToTagMapsEveryCode) {
    EXPECT_EQ(toTag(StartupErrorCode::ConfigMissing),   "CONFIG MISSING");
    EXPECT_EQ(toTag(StartupErrorCode::ConfigCorrupt),   "CONFIG CORRUPT");
    EXPECT_EQ(toTag(StartupErrorCode::DatabaseInit),    "DATABASE ERROR");
    EXPECT_EQ(toTag(StartupErrorCode::LoggerBootstrap), "LOGGER ERROR");
}

TEST(StartupErrorsTest, TagsAreDistinct) {
    // If someone accidentally collapses two tags into the same string,
    // the startup dialog title and exit-log category would merge. Make
    // that regression loud.
    const std::string_view tags[] = {
        toTag(StartupErrorCode::ConfigMissing),
        toTag(StartupErrorCode::ConfigCorrupt),
        toTag(StartupErrorCode::DatabaseInit),
        toTag(StartupErrorCode::LoggerBootstrap),
    };
    for (std::size_t i = 0; i < std::size(tags); ++i) {
        for (std::size_t j = i + 1; j < std::size(tags); ++j) {
            EXPECT_NE(tags[i], tags[j])
                << "Tags " << i << " and " << j << " collided: " << tags[i];
        }
    }
}

// Base CriticalStartupError

TEST(StartupErrorsTest, BaseCtorPreservesCodeAndMessage) {
    CriticalStartupError e{StartupErrorCode::DatabaseInit,
                           "something broke"};
    EXPECT_EQ(e.code(), StartupErrorCode::DatabaseInit);
    EXPECT_STREQ(e.what(), "something broke");
}

TEST(StartupErrorsTest, BaseIsCatchableAsStdException) {
    try {
        throw CriticalStartupError{StartupErrorCode::ConfigMissing, "x"};
    } catch (const std::exception& e) {
        SUCCEED();
        EXPECT_STREQ(e.what(), "x");
        return;
    }
    FAIL() << "Expected std::exception to catch";
}

// Concrete subclasses — each one carries its specific code and can be
// caught as the base type (which is what main.cpp relies on).

TEST(StartupErrorsTest, ConfigMissingErrorCarriesCode) {
    ConfigMissingError e{"missing"};
    EXPECT_EQ(e.code(), StartupErrorCode::ConfigMissing);
    EXPECT_STREQ(e.what(), "missing");
}

TEST(StartupErrorsTest, ConfigCorruptErrorCarriesCode) {
    ConfigCorruptError e{"corrupt"};
    EXPECT_EQ(e.code(), StartupErrorCode::ConfigCorrupt);
    EXPECT_STREQ(e.what(), "corrupt");
}

TEST(StartupErrorsTest, DatabaseInitErrorCarriesCode) {
    DatabaseInitError e{"db"};
    EXPECT_EQ(e.code(), StartupErrorCode::DatabaseInit);
    EXPECT_STREQ(e.what(), "db");
}

TEST(StartupErrorsTest, LoggerBootstrapErrorCarriesCode) {
    LoggerBootstrapError e{"log"};
    EXPECT_EQ(e.code(), StartupErrorCode::LoggerBootstrap);
    EXPECT_STREQ(e.what(), "log");
}

TEST(StartupErrorsTest, SubclassesCatchableAsBase) {
    // main.cpp has `catch (const CriticalStartupError& e)` — every
    // concrete subclass must reach that handler.
    try { throw ConfigMissingError{"a"}; }
    catch (const CriticalStartupError&) { SUCCEED(); }
    catch (...) { FAIL() << "ConfigMissingError escaped base handler"; }

    try { throw ConfigCorruptError{"b"}; }
    catch (const CriticalStartupError&) { SUCCEED(); }
    catch (...) { FAIL() << "ConfigCorruptError escaped base handler"; }

    try { throw DatabaseInitError{"c"}; }
    catch (const CriticalStartupError&) { SUCCEED(); }
    catch (...) { FAIL() << "DatabaseInitError escaped base handler"; }

    try { throw LoggerBootstrapError{"d"}; }
    catch (const CriticalStartupError&) { SUCCEED(); }
    catch (...) { FAIL() << "LoggerBootstrapError escaped base handler"; }
}
