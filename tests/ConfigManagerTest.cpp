// [utest->req~core-004~1]
// [utest->req~core-006~1]
// [utest->req~multistation-001~1]
// Covers REQ-CORE-004 (config from JSON, validated),
//             REQ-CORE-006 (hot reload Phase 1: atomic re-read +
//             re-validate + rollback on rejection),
//             REQ-MULTISTATION-001 (multi-station opt-in via config).
//
// Tests for app::config::ConfigManager
// Covers JSON load, getLanguage default, setLanguage in-memory + persistence.
//
// ConfigManager is a singleton; the SetUp fixture calls clear() on every test
// to avoid state bleeding between runs. Each test owns a unique temp file so
// parallel ctest invocations don't collide on disk.

#include "src/config/ConfigManager.h"
#include "src/config/config_defaults.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

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

// Fixture - manages a unique temp file per test + resets singleton state

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

// getLanguage()

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

// setLanguage() - in-memory update

TEST_F(ConfigManagerTest, SetLanguageUpdatesInMemoryValueImmediately) {
    writeFile(tmpPath_, baseConfig("it"));

    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    ASSERT_EQ(cfg.getLanguage(), "it");

    ASSERT_TRUE(cfg.setLanguage("fr"));
    EXPECT_EQ(cfg.getLanguage(), "fr");
}

// setLanguage() - persistence

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

// Application / Window getters - sanity check that parse reaches other keys

TEST_F(ConfigManagerTest, ReadsApplicationNameFromConfig) {
    writeFile(tmpPath_, baseConfig("auto"));

    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    EXPECT_EQ(cfg.getAppName(), "Industrial HMI");
}

// reload() -- REQ-CORE-006 Phase 1 (hot reload, atomic re-read + re-validate)

TEST_F(ConfigManagerTest, ReloadAppliesNewLanguageFromEditedFile) {
    writeFile(tmpPath_, baseConfig("it"));
    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    EXPECT_EQ(cfg.getLanguage(), "it");

    // Operator edits the file in place.
    writeFile(tmpPath_, baseConfig("de"));
    ASSERT_TRUE(cfg.reload());
    EXPECT_EQ(cfg.getLanguage(), "de");
}

TEST_F(ConfigManagerTest, ReloadReturnsFalseAndKeepsConfigWhenFileMissing) {
    writeFile(tmpPath_, baseConfig("it"));
    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    ASSERT_EQ(cfg.getLanguage(), "it");

    // Delete the file behind ConfigManager's back -- the file system
    // can disappear under us (mount point, race with editor saving).
    std::error_code ec;
    fs::remove(tmpPath_, ec);

    EXPECT_FALSE(cfg.reload());
    // Previous in-memory config still applies -- consumer's getter
    // calls do not see "auto" fallback.
    EXPECT_EQ(cfg.getLanguage(), "it");
}

TEST_F(ConfigManagerTest, ReloadReturnsFalseAndKeepsConfigOnParseError) {
    writeFile(tmpPath_, baseConfig("it"));
    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    ASSERT_EQ(cfg.getLanguage(), "it");

    // Truncated JSON -- the operator's editor crashed mid-save.
    writeFile(tmpPath_, "{ \"i18n\": { \"language\": \"de\"");

    EXPECT_FALSE(cfg.reload());
    EXPECT_EQ(cfg.getLanguage(), "it") << "Parse failure must not "
                                          "half-apply the new file.";
}

TEST_F(ConfigManagerTest, ReloadReturnsFalseAndKeepsConfigOnValidatorRejection) {
    writeFile(tmpPath_, baseConfig("it"));
    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));
    ASSERT_EQ(cfg.getLanguage(), "it");

    // Syntactically valid JSON but semantically rejected by
    // ConfigValidator: "klingon" is not a recognised LINGUAS code.
    writeFile(tmpPath_,
              "{\n"
              "  \"i18n\": { \"language\": \"klingon\" }\n"
              "}\n");

    EXPECT_FALSE(cfg.reload());
    // The validator-rejected file must NOT replace the previous valid
    // state -- an operator typo never half-applies.
    EXPECT_EQ(cfg.getLanguage(), "it");
}

TEST_F(ConfigManagerTest, ReloadReturnsFalseWhenInitializeNeverRan) {
    // Programmer-error path: someone calls reload() before initialize()
    // ever set a path. The method must not crash + must not pretend it
    // succeeded.
    auto& cfg = ConfigManager::instance();
    EXPECT_FALSE(cfg.reload());
}

// [utest->req~core-008~1]
// REQ-CORE-008: ConfigManager guards `config_` with an internal mutex so
// concurrent readers + a writer (`reload()`, typically from
// ConfigFileWatcher's background thread) are race-free. The pre-Phase-3a
// invariant was "single-writer + single-reader by convention"; this test
// proves the new invariant by spinning a reader loop on one thread while
// the main thread hammers reload() on the same singleton. Under TSan
// (-fsanitize=thread, wired in CI as ctest --label-regex tsan) this
// would have flagged on every iteration before Phase 3a.
TEST_F(ConfigManagerTest, ConcurrentReadersDuringReload) {
    // Skip under Valgrind memcheck: the file I/O + reader-thread spin
    // loop runs ~20-50x slower under valgrind and tips the per-test
    // 300s ctest --memcheck timeout. The test exists to catch a TSan
    // race, not a memory leak, so memcheck adds no signal here. CI
    // sets RUNNING_UNDER_VALGRIND=1 specifically for the memcheck job
    // (see .github/workflows/ci.yml).
    if (const char* v = std::getenv("RUNNING_UNDER_VALGRIND");
        v != nullptr && std::string_view(v) == "1") {
        GTEST_SKIP() << "ConcurrentReadersDuringReload skipped under "
                        "Valgrind memcheck (TSan covers this case).";
    }

    const auto path = fs::temp_directory_path() /
                      "industrial-hmi-concurrent.json";
    {
        std::ofstream out(path, std::ios::trunc);
        out << baseConfig("it");
    }
    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(path.string()));

    std::atomic<bool> stop{false};
    std::atomic<std::size_t> reads{0};
    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            // Three different access paths: a flat getter, an int getter,
            // a dialog lookup. They all funnel through the same mutex.
            const auto lang = cfg.getLanguage();
            (void)lang;
            (void)cfg.getWindowWidth();
            (void)cfg.getDialogMessage("confirm", "exit");
            reads.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Hammer reload() from the main thread. 50 iterations is plenty
    // to expose a race under TSan -- the runtime is sensitive to a
    // single unsynchronised access. Under Valgrind memcheck this would
    // tip the 300s per-test ctest timeout, but the early
    // RUNNING_UNDER_VALGRIND guard above already skips this case for
    // memcheck (a race-test gives no signal under memcheck anyway).
    const int kReloadIterations = 50;
    for (int i = 0; i < kReloadIterations; ++i) {
        // Alternate between two valid configs so the validator stays
        // happy on every iteration -- we want to exercise the swap +
        // validate path, not the rollback path.
        std::ofstream out(path, std::ios::trunc);
        out << baseConfig((i % 2 == 0) ? "de" : "it");
        out.close();
        EXPECT_TRUE(cfg.reload());
    }

    stop.store(true, std::memory_order_relaxed);
    reader.join();

    // Sanity: the reader actually got CPU time. We don't assert a
    // specific count -- scheduler-dependent -- just that it observed
    // SOME iterations, so the test really did exercise concurrency.
    EXPECT_GT(reads.load(), 0u);

    // Final state must be one of the two valid values; the reload loop
    // ended with i==49 -> "it".
    EXPECT_EQ(cfg.getLanguage(), "it");

    std::error_code ec;
    fs::remove(path, ec);
}
