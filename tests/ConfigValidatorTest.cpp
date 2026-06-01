// [utest->req~core-005~1]
// Covers REQ-CORE-005 (semantic validation of app-config.json).
//
// Tests for app::config::ConfigValidator. Each test writes a minimal
// JSON config to a unique temp file, initialises the ConfigManager
// singleton against it, and asserts the validator either accepts or
// rejects the specific value under test. We rely on ConfigManager's
// own initialize() to populate the in-memory map -- so a failing
// validator test that points at JSON the manager couldn't parse is
// telling us about loadConfig, not the rule.

#include "src/config/ConfigManager.h"
#include "src/config/ConfigValidator.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using app::config::ConfigManager;
using app::config::ConfigValidator;

namespace {

void writeFile(const fs::path& p, const std::string& content) {
    std::ofstream out(p, std::ios::trunc);
    out << content;
}

/// Wrap a body of `"section": { ... }` blocks in the top-level braces
/// expected by ConfigManager. Saves on string concatenation noise in
/// every test.
std::string wrap(const std::string& body) {
    std::ostringstream os;
    os << "{\n" << body << "\n}\n";
    return os.str();
}

class ConfigValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        std::string name = info ? info->name() : "unnamed";
        tmpPath_ = fs::temp_directory_path() /
                   ("industrial-hmi-validator-" + name + ".json");
        ConfigManager::instance().clear();
    }

    void TearDown() override {
        ConfigManager::instance().clear();
        std::error_code ec;
        fs::remove(tmpPath_, ec);
    }

    fs::path tmpPath_;
};

}  // namespace

TEST_F(ConfigValidatorTest, AcceptsMinimalValidConfig) {
    // A bare config relies on every default in ConfigManager. Defaults
    // are deliberately chosen to pass validation -- a fresh install
    // must never trip ConfigInvalid.
    writeFile(tmpPath_, "{}\n");
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_TRUE(r.ok) << "first error: "
                      << (r.errors.empty() ? "<none>" : r.errors.front());
}

TEST_F(ConfigValidatorTest, RejectsUnknownLogLevel) {
    writeFile(tmpPath_, wrap(R"(  "logging": { "level": "VERBOSE" })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    // We don't require an exact string match -- the prefix is part of
    // the contract the operator dialog displays. The rest is wording.
    ASSERT_FALSE(r.errors.empty());
    EXPECT_NE(r.errors.front().find("logging.level"), std::string::npos);
}

TEST_F(ConfigValidatorTest, RejectsUnknownLanguageCode) {
    writeFile(tmpPath_, wrap(R"(  "i18n": { "language": "klingon" })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_NE(r.errors.front().find("i18n.language"), std::string::npos);
}

TEST_F(ConfigValidatorTest, AcceptsAutoLanguage) {
    writeFile(tmpPath_, wrap(R"(  "i18n": { "language": "auto" })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_TRUE(r.ok);
}

TEST_F(ConfigValidatorTest, RejectsNonPositiveWindowDimensions) {
    writeFile(tmpPath_, wrap(R"(  "window": { "default_width": 0, "default_height": -1 })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    // Two rules tripped (width AND height) -- validator must collect
    // every violation, not bail on the first.
    EXPECT_EQ(r.errors.size(), 2u);
}

TEST_F(ConfigValidatorTest, RejectsOutOfRangeTcpPortWhenEnabled) {
    writeFile(tmpPath_, wrap(R"(  "network": { "tcp": { "enabled": true, "port": 70000 } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_NE(r.errors.front().find("network.tcp.port"), std::string::npos);
}

TEST_F(ConfigValidatorTest, IgnoresOutOfRangeTcpPortWhenDisabled) {
    // Same bogus port -- but enabled=false. Validator must not block
    // startup over a backend the operator isn't turning on.
    writeFile(tmpPath_, wrap(R"(  "network": { "tcp": { "enabled": false, "port": 70000 } })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_TRUE(r.ok);
}

TEST_F(ConfigValidatorTest, RejectsHistorianBatchSizeZeroWhenEnabled) {
    writeFile(tmpPath_, wrap(R"(  "historian": { "enabled": true, "batch_size": 0, "batch_age_ms": 1000, "sweep_interval_ms": 1000 })"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    ASSERT_FALSE(r.errors.empty());
    EXPECT_NE(r.errors.front().find("historian.batch_size"),
              std::string::npos);
}

TEST_F(ConfigValidatorTest, CollectsAllViolations) {
    // Multiple unrelated bad values -- assert the validator returns
    // every one. Operators get one round-trip instead of a fix-and-retry
    // loop.
    writeFile(tmpPath_, wrap(R"(
      "logging": { "level": "PANIC" },
      "i18n":    { "language": "xx" },
      "window":  { "default_width": -5 }
    )"));
    ASSERT_TRUE(ConfigManager::instance().initialize(tmpPath_.string()));

    auto r = ConfigValidator::validate(ConfigManager::instance());
    EXPECT_FALSE(r.ok);
    EXPECT_GE(r.errors.size(), 3u);
}
