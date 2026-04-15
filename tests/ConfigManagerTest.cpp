// Tests for app::config::ConfigManager
// Covers JSON load, getLanguage default, setLanguage in-memory + persistence.
//
// ConfigManager is a singleton; the SetUp fixture calls clear() on every test
// to avoid state bleeding between runs. Each test owns a unique temp file so
// parallel ctest invocations don't collide on disk.

#include "src/config/ConfigManager.h"
#include "src/config/config_defaults.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>

namespace fs = std::filesystem;
using app::config::ConfigManager;

namespace {

// Simple minimal JSON that matches the flat shape parsed by ConfigManager.
// The parser is line-oriented; indentation and ordering matter less than
// having one "key": value pair per line.
std::string baseConfig(const std::string& languageValue) {
    std::ostringstream os;
    os << "{\n"
       << "  \"application\": {\n"
       << "    \"name\": \"Industrial HMI\"\n"
       << "  },\n"
       << "  \"i18n\": {\n"
       << "    \"language\": \"" << languageValue << "\"\n"
       << "  }\n"
       << "}\n";
    return os.str();
}

std::string configWithoutI18n() {
    return
        "{\n"
        "  \"application\": {\n"
        "    \"name\": \"Industrial HMI\"\n"
        "  }\n"
        "}\n";
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

void writeFile(const fs::path& p, const std::string& content) {
    std::ofstream out(p, std::ios::trunc);
    out << content;
}

}  // namespace

// ============================================================================
// Fixture - manages a unique temp file per test + resets singleton state
// ============================================================================

class ConfigManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Fresh path per test - uses gtest's test name so failures point at
        // an inspectable file when something goes wrong.
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string name = info ? info->name() : "unnamed";
        tmpPath_ = fs::temp_directory_path() /
                   ("industrial-hmi-config-" + name + ".json");

        // Reset singleton state so stale keys from previous tests don't bleed
        ConfigManager::instance().clear();
    }

    void TearDown() override {
        ConfigManager::instance().clear();
        std::error_code ec;
        fs::remove(tmpPath_, ec);  // best-effort cleanup
    }

    fs::path tmpPath_;
};

// ============================================================================
// getLanguage()
// ============================================================================

TEST_F(ConfigManagerTest, LoadsLanguageFromConfigFile) {
    writeFile(tmpPath_, baseConfig("it"));

    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    EXPECT_EQ(cfg.getLanguage(), "it");
}

TEST_F(ConfigManagerTest, LoadsAnyLanguageCode) {
    writeFile(tmpPath_, baseConfig("pt_BR"));

    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    EXPECT_EQ(cfg.getLanguage(), "pt_BR");
}

TEST_F(ConfigManagerTest, DefaultsToAutoWhenKeyMissing) {
    writeFile(tmpPath_, configWithoutI18n());

    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    EXPECT_EQ(cfg.getLanguage(), app::config::defaults::kDefaultLanguage);
    EXPECT_EQ(cfg.getLanguage(), "auto");
}

TEST_F(ConfigManagerTest, DefaultsToAutoWhenInitializeFails) {
    auto missing = fs::temp_directory_path() / "industrial-hmi-does-not-exist.json";
    std::error_code ec;
    fs::remove(missing, ec);  // make sure it really doesn't exist

    auto& cfg = ConfigManager::instance();
    EXPECT_FALSE(cfg.initialize(missing.string()));
    EXPECT_EQ(cfg.getLanguage(), "auto");
}

// ============================================================================
// setLanguage() - in-memory update
// ============================================================================

TEST_F(ConfigManagerTest, SetLanguageUpdatesInMemoryValueImmediately) {
    writeFile(tmpPath_, baseConfig("it"));

    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    ASSERT_EQ(cfg.getLanguage(), "it");

    ASSERT_TRUE(cfg.setLanguage("fr"));
    EXPECT_EQ(cfg.getLanguage(), "fr");
}

// ============================================================================
// setLanguage() - persistence
// ============================================================================

TEST_F(ConfigManagerTest, SetLanguagePersistsToDisk) {
    writeFile(tmpPath_, baseConfig("it"));

    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    ASSERT_TRUE(cfg.setLanguage("de"));

    // Re-read the file and confirm the new value landed on disk
    const std::string onDisk = readFile(tmpPath_);
    EXPECT_NE(onDisk.find("\"language\": \"de\""), std::string::npos)
        << "Expected 'language: de' in file; got:\n" << onDisk;
    EXPECT_EQ(onDisk.find("\"it\""), std::string::npos)
        << "Old value 'it' should have been replaced";
}

TEST_F(ConfigManagerTest, SetLanguageSurvivesReload) {
    writeFile(tmpPath_, baseConfig("it"));

    {
        auto& cfg = ConfigManager::instance();
        ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
        ASSERT_TRUE(cfg.setLanguage("sv"));
    }

    // Simulate a process restart: clear in-memory state, reload the file.
    ConfigManager::instance().clear();
    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    EXPECT_EQ(cfg.getLanguage(), "sv");
}

TEST_F(ConfigManagerTest, SetLanguageInsertsI18nSectionWhenMissing) {
    writeFile(tmpPath_, configWithoutI18n());

    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    ASSERT_EQ(cfg.getLanguage(), "auto");  // sanity: no i18n block yet

    ASSERT_TRUE(cfg.setLanguage("es"));

    const std::string onDisk = readFile(tmpPath_);
    EXPECT_NE(onDisk.find("\"i18n\""), std::string::npos)
        << "Expected i18n section to be injected";
    EXPECT_NE(onDisk.find("\"language\": \"es\""), std::string::npos);
}

// ============================================================================
// Application / Window getters - sanity check that parse reaches other keys
// ============================================================================

TEST_F(ConfigManagerTest, ReadsApplicationNameFromConfig) {
    writeFile(tmpPath_, baseConfig("auto"));

    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    EXPECT_EQ(cfg.getAppName(), "Industrial HMI");
}
