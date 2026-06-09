// SPDX-License-Identifier: MIT
//
// ConfigFileWatcher -- Phase 2 of REQ-CORE-006 hot-reload story
// (implementation). See `ConfigFileWatcher.h` for design notes.

#include "ConfigFileWatcher.h"

#include "ConfigManager.h"
#include "src/core/LoggerBase.h"

#include <system_error>
#include <utility>

namespace app::config {

ConfigFileWatcher::ConfigFileWatcher(ConfigManager& cfg,
                                     std::filesystem::path path,
                                     std::chrono::milliseconds interval)
    : cfg_(cfg),
      path_(std::move(path)),
      interval_(interval),
      lastSeenMtime_(readMtime()) {}

ConfigFileWatcher::~ConfigFileWatcher() {
    stop();
}

void ConfigFileWatcher::setLogger(app::core::Logger& logger) {
    logger_ = &logger;
}

void ConfigFileWatcher::start() {
    if (running_.exchange(true)) return;  // already running -- no-op
    thread_ = std::jthread([this](std::stop_token st) { loop(st); });
}

void ConfigFileWatcher::stop() noexcept {
    if (!running_.exchange(false)) return;  // already stopped
    if (thread_.joinable()) {
        thread_.request_stop();
        // Wake the loop's condition_variable so it sees the stop token
        // immediately instead of waiting out the full interval.
        {
            const std::scoped_lock lock(cvMutex_);
        }
        cv_.notify_all();
        thread_.join();
    }
}

bool ConfigFileWatcher::isRunning() const noexcept {
    return running_.load(std::memory_order_relaxed);
}

bool ConfigFileWatcher::pollOnce() {
    const auto current = readMtime();
    if (current == lastSeenMtime_) {
        // File still has the same mtime -- nothing to do.
        return false;
    }

    // mtime changed (either the file was edited, or it briefly
    // disappeared during a temp + rename save). Remember the new
    // value BEFORE invoking reload so a manager-side failure does
    // not make us re-trigger on every subsequent poll: the watcher's
    // contract is "tell the manager the file changed", not "retry
    // until the manager accepts it".
    lastSeenMtime_ = current;

    if (logger_) {
        logger_->info("ConfigFileWatcher: '{}' changed; triggering reload",
                      path_.string());
    }

    // reload() is documented to never throw and to roll back on any
    // failure (REQ-CORE-006 Phase 1). The watcher does not interpret
    // the return value beyond logging at INFO when reload accepted /
    // WARN when it rejected -- the manager's own log line already
    // describes the rejection reason.
    const bool reloaded = cfg_.reload();
    if (logger_ && !reloaded) {
        logger_->warn("ConfigFileWatcher: reload of '{}' was rejected; "
                      "previous config still applies",
                      path_.string());
    }

    return true;
}

void ConfigFileWatcher::loop(std::stop_token stop) {
    while (!stop.stop_requested()) {
        // Sleep on cv_ keyed to the stop token. This collapses the
        // typical "while(!stop) { sleep; tick; }" pattern's stop-
        // latency from `interval` to "as soon as stop() runs".
        {
            std::unique_lock<std::mutex> lock(cvMutex_);
            cv_.wait_for(lock, stop, interval_,
                         [&stop]() { return stop.stop_requested(); });
        }
        if (stop.stop_requested()) break;
        // Swallow exceptions defensively -- the watcher must not be
        // the reason the binary crashes. reload() itself is noexcept-
        // ish (catches its own JSON parse errors) but
        // std::filesystem::last_write_time can throw on permission
        // changes, mount point gymnastics, etc.
        try {
            (void)pollOnce();
        } catch (const std::exception& e) {
            if (logger_) {
                logger_->warn("ConfigFileWatcher: poll exception ('{}'); "
                              "continuing",
                              e.what());
            }
        } catch (...) {
            // Non-std exception (e.g. a third-party library throwing a
            // foreign type, or a `throw 42;` somewhere). Log a generic
            // message and continue rather than letting an unknown
            // throw kill the worker -- the watcher's contract is
            // "notice file changes"; bringing the binary down on the
            // wrong shape of exception loses signal more than it
            // gains. Logging keeps the event observable for triage.
            if (logger_) {
                logger_->warn("ConfigFileWatcher: poll exception "
                              "(non-std type); continuing");
            }
        }
    }
}

std::filesystem::file_time_type ConfigFileWatcher::readMtime() const {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(path_, ec);
    if (ec) {
        // File missing or unreadable. Returning min() means the next
        // successful read will compare different and trigger a
        // reload (which is what the operator expects when the file
        // re-appears via temp + rename).
        return std::filesystem::file_time_type::min();
    }
    return t;
}

}  // namespace app::config
