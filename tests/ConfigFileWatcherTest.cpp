// [utest->req~core-007~1]
// Covers REQ-CORE-007 (config file watcher: notice mtime change +
// invoke ConfigManager::reload).
//
// Tests for app::config::ConfigFileWatcher.
//
// The watcher exposes `pollOnce()` so tests can drive iterations
// deterministically without starting the background thread (the same
// pattern HistorianMaintenance::runOnce() uses). The background-thread
// lifecycle (start / stop / RAII) gets one separate test that exercises
// the thread explicitly with a short interval.

#include "src/config/ConfigFileWatcher.h"
#include "src/config/ConfigManager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using app::config::ConfigFileWatcher;
using app::config::ConfigManager;

namespace {

void writeFile(const fs::path& p, const std::string& content) {
    std::ofstream out(p, std::ios::trunc);
    out << content;
}

std::string configWith(const std::string& languageValue) {
    return std::string{"{\n"}
        + "  \"i18n\": { \"language\": \"" + languageValue + "\" }\n"
        + "}\n";
}

/// Advance the file's mtime forward by `delta` so a fresh stat sees a
/// later timestamp. Using std::filesystem::last_write_time directly
/// is portable across platforms; a touch shell-out would not be.
void bumpMtime(const fs::path& p, std::chrono::seconds delta) {
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(p, now + delta);
}

}  // namespace

class ConfigFileWatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        const std::string name = info ? info->name() : "unnamed";
        tmpPath_ = fs::temp_directory_path() /
                   ("industrial-hmi-watch-" + name + ".json");

        ConfigManager::instance().clear();
    }

    void TearDown() override {
        ConfigManager::instance().clear();
        std::error_code ec;
        fs::remove(tmpPath_, ec);
    }

    fs::path tmpPath_;
};

TEST_F(ConfigFileWatcherTest, PollOnceReturnsFalseWhenFileUnchanged) {
    writeFile(tmpPath_, configWith("it"));
    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));

    ConfigFileWatcher watcher(cfg, tmpPath_);
    EXPECT_FALSE(watcher.pollOnce())
        << "Initial poll should observe the same mtime captured at "
           "construction time and report no change.";
    EXPECT_EQ(cfg.getLanguage(), "it");
}

TEST_F(ConfigFileWatcherTest, PollOnceTriggersReloadOnMtimeChange) {
    writeFile(tmpPath_, configWith("it"));
    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));

    ConfigFileWatcher watcher(cfg, tmpPath_);

    // Operator edits the file; bumpMtime makes the file system see a
    // later last-write-time without needing a sleep.
    writeFile(tmpPath_, configWith("de"));
    bumpMtime(tmpPath_, std::chrono::seconds{2});

    EXPECT_TRUE(watcher.pollOnce());
    EXPECT_EQ(cfg.getLanguage(), "de");
}

TEST_F(ConfigFileWatcherTest, PollOnceDoesNotRetryAfterUnchangedMtime) {
    writeFile(tmpPath_, configWith("it"));
    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));

    ConfigFileWatcher watcher(cfg, tmpPath_);

    // First change -> reload triggers.
    writeFile(tmpPath_, configWith("de"));
    bumpMtime(tmpPath_, std::chrono::seconds{2});
    ASSERT_TRUE(watcher.pollOnce());

    // Second poll with no further edit -> watcher remembers the new
    // mtime and reports no change.
    EXPECT_FALSE(watcher.pollOnce());
    EXPECT_EQ(cfg.getLanguage(), "de");
}

TEST_F(ConfigFileWatcherTest, PollOnceReturnsTrueEvenWhenReloadRejected) {
    writeFile(tmpPath_, configWith("it"));
    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));

    ConfigFileWatcher watcher(cfg, tmpPath_);

    // Write a semantically-invalid config: "klingon" fails the
    // ConfigValidator language-code check. The watcher's contract is
    // "the file changed", regardless of whether the manager accepted
    // the new contents.
    writeFile(tmpPath_, configWith("klingon"));
    bumpMtime(tmpPath_, std::chrono::seconds{2});

    EXPECT_TRUE(watcher.pollOnce());
    // ConfigManager rolled back; getter returns the original value.
    EXPECT_EQ(cfg.getLanguage(), "it");
}

TEST_F(ConfigFileWatcherTest, PollOnceHandlesMissingFile) {
    writeFile(tmpPath_, configWith("it"));
    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));

    ConfigFileWatcher watcher(cfg, tmpPath_);

    // Delete the file (editor uses temp + rename and the rename has
    // not landed yet, or someone literally deleted the file).
    std::error_code ec;
    fs::remove(tmpPath_, ec);

    // The mtime now reads as the sentinel `min()`. The watcher must
    // report "changed" and trigger reload; reload returns false and
    // the previous in-memory config is preserved.
    EXPECT_TRUE(watcher.pollOnce());
    EXPECT_EQ(cfg.getLanguage(), "it") << "Missing file must not "
                                          "drop the previous config.";
}

TEST_F(ConfigFileWatcherTest, BackgroundThreadStartsAndStops) {
    writeFile(tmpPath_, configWith("it"));
    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));

    ConfigFileWatcher watcher(cfg, tmpPath_, std::chrono::milliseconds{50});
    EXPECT_FALSE(watcher.isRunning());

    watcher.start();
    EXPECT_TRUE(watcher.isRunning());

    // Let the background thread spin briefly so we exercise the
    // condition_variable wake path on stop. 100 ms is two poll
    // intervals -- long enough that we're confident the loop body
    // ran at least once, short enough not to slow ctest.
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    watcher.stop();
    EXPECT_FALSE(watcher.isRunning());

    // Idempotent stop.
    watcher.stop();
    EXPECT_FALSE(watcher.isRunning());
}

TEST_F(ConfigFileWatcherTest, BackgroundThreadDetectsEdit) {
    writeFile(tmpPath_, configWith("it"));
    auto& cfg = ConfigManager::instance();
    ASSERT_TRUE(cfg.initialize(tmpPath_.string()));

    ConfigFileWatcher watcher(cfg, tmpPath_, std::chrono::milliseconds{25});
    watcher.start();

    // Edit + force-future mtime.
    writeFile(tmpPath_, configWith("de"));
    bumpMtime(tmpPath_, std::chrono::seconds{2});

    // Poll up to ~500 ms for the change to land. Avoids a hard sleep
    // by checking the side-effect (cfg.getLanguage() == "de").
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds{500};
    while (std::chrono::steady_clock::now() < deadline) {
        if (cfg.getLanguage() == "de") break;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    EXPECT_EQ(cfg.getLanguage(), "de");
    watcher.stop();
}
