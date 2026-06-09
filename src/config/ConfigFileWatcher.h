#ifndef CONFIG_FILE_WATCHER_H
#define CONFIG_FILE_WATCHER_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace app::core {
class Logger;
}

namespace app::config {

class ConfigManager;

/// ConfigFileWatcher -- Phase 2 of REQ-CORE-006 hot-reload story.
///
/// Polls `path` for last-write-time changes at `interval` cadence; when
/// the mtime moves, calls `cfgManager.reload()`. ConfigManager handles
/// parse + validation + atomic swap + rollback internally (REQ-CORE-006
/// Phase 1) -- the watcher's only job is "notice the file changed and
/// poke the manager".
///
/// @design Polling instead of inotify / kqueue / ReadDirectoryChangesW
/// for three reasons:
///   1. Cross-platform without per-OS conditional code paths. The HMI
///      ships on Linux + Windows (MSYS2 CLANG64) + headless console;
///      one polling implementation works on all three.
///   2. The cadence the operator perceives (~1 s) is the cadence the
///      poll runs at. Event-driven would arrive faster, but for a
///      config file edited by hand, sub-second latency buys nothing.
///   3. The polling loop is trivial to test deterministically -- call
///      `pollOnce()` from the test thread between file edits. An
///      event-driven implementation would need a fake-event injector.
///
/// @lifecycle Owns one `std::jthread`. `start()` spawns it; `stop()`
/// joins; the destructor calls `stop()` so RAII covers the typical
/// "watcher member in a composition root" usage. Each loop iteration
/// sleeps on a `condition_variable` keyed to the stop token so
/// shutdown returns in ms, not the full interval.
///
/// Why a dedicated thread (vs a Glib timer): same reason
/// HistorianMaintenance ships one. The GTK timer only fires on the
/// GTK main loop, which the console binary doesn't have. A
/// `std::jthread` works in both front-ends.
///
/// @thread_safety The watcher's own state is mutex-protected. Since
/// Phase 3a (REQ-CORE-008), ConfigManager guards `config_` with an
/// internal `std::recursive_mutex` across every read and write, so
/// concurrent readers on any thread (GTK main loop, integration
/// backends, future console workers) are race-free against the
/// watcher's background `reload()` call. ThreadSanitizer is wired in
/// CI and `ConfigManagerTest.ConcurrentReadersDuringReload` exercises
/// the pattern explicitly. The earlier single-writer constraint
/// (Phase 2 of REQ-CORE-006) no longer applies.
class ConfigFileWatcher {
public:
    /// Default polling cadence -- 1 s matches the operator's
    /// perception of "instant" without flogging the disk.
    static constexpr std::chrono::milliseconds kDefaultInterval{1000};

    ConfigFileWatcher(ConfigManager& cfg,
                      std::filesystem::path path,
                      std::chrono::milliseconds interval = kDefaultInterval);

    ~ConfigFileWatcher();

    ConfigFileWatcher(const ConfigFileWatcher&)            = delete;
    ConfigFileWatcher& operator=(const ConfigFileWatcher&) = delete;
    ConfigFileWatcher(ConfigFileWatcher&&)                 = delete;
    ConfigFileWatcher& operator=(ConfigFileWatcher&&)      = delete;

    /// Inject a logger so the watcher can report reload outcomes
    /// through the normal pipeline. Without one, the watcher stays
    /// silent (reload() itself logs through ConfigManager's injected
    /// logger if that was wired separately).
    void setLogger(app::core::Logger& logger);

    /// Spawn the background thread. Idempotent: subsequent calls are
    /// no-ops while the worker is already running.
    void start();

    /// Stop the background thread + join. Idempotent. Safe from the
    /// destructor. Returns within a poll interval at worst (the
    /// condition_variable wakes the loop immediately).
    void stop() noexcept;

    /// Run ONE poll iteration on the caller's thread. Exposed so unit
    /// tests can drive the watcher deterministically between
    /// `writeFile` calls without starting the background thread.
    /// Returns whether a reload was triggered (true) or the mtime was
    /// unchanged (false). When a reload is triggered, the return value
    /// does NOT indicate whether ConfigManager accepted the new
    /// contents -- the watcher knows the file changed, the manager
    /// decides whether the change applies.
    bool pollOnce();

    /// True between `start()` and `stop()`. For tests + diagnostics.
    [[nodiscard]] bool isRunning() const noexcept;

private:
    void loop(std::stop_token stop);

    /// Read the file's last-write-time. Returns
    /// `file_time_type::min()` when the file does not exist (the
    /// editor saved via temp + rename, or somebody deleted it). The
    /// sentinel value compares less than any real timestamp so the
    /// next poll that sees the file back will detect the change.
    [[nodiscard]] std::filesystem::file_time_type readMtime() const;

    ConfigManager&                        cfg_;
    std::filesystem::path                 path_;
    std::chrono::milliseconds             interval_;
    app::core::Logger*                    logger_{nullptr};

    std::filesystem::file_time_type       lastSeenMtime_{};

    std::jthread                          thread_;
    mutable std::mutex                    cvMutex_;
    std::condition_variable_any           cv_;
    std::atomic<bool>                     running_{false};
};

}  // namespace app::config

#endif  // CONFIG_FILE_WATCHER_H
