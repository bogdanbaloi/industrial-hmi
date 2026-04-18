#ifndef LOGGER_BASE_H
#define LOGGER_BASE_H

#include <string>
#include <string_view>
#include <format>
#include <source_location>
#include <memory>
#include <vector>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <concepts>
#include <type_traits>

namespace app::core {

/**
 * Log Levels
 *
 * Ordered from most verbose to most severe. Setting the logger to LEVEL
 * suppresses every message with severity below LEVEL.
 *
 * Convention in this codebase:
 *   TRACE    - per-callback / per-signal / per-VM-build firehose. Used
 *              to follow the MVP event chain end-to-end during debugging.
 *   DEBUG    - developer-oriented details: user actions, VM snapshots,
 *              internal state transitions worth seeing when diagnosing.
 *   INFO     - production-worthy lifecycle events: init / shutdown,
 *              explicit user commands (Start / Stop), config changes.
 *   WARN     - recoverable anomalies; code took a fallback path.
 *   ERROR    - user-visible failures; an operation did not complete.
 *   CRITICAL - programmer errors or corruption; investigate immediately.
 */
enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL
};

/**
 * LoggerBase - Abstract Logger (SOLID: Interface Segregation)
 * 
 * LLVM/Clang naming convention: No "I" prefix
 * 
 * Abstract base class for logging implementations
 * Enables dependency injection and testability
 * 
 * Implementations:
 * - ConsoleLogger: Output to stdout/stderr
 * - FileLogger: Output to file with rotation
 * - CompositeLogger: Multiple loggers combined
 * - NullLogger: No-op for testing
 */
class LoggerBase {
public:
    virtual ~LoggerBase() = default;
    LoggerBase(const LoggerBase&) = delete;
    LoggerBase& operator=(const LoggerBase&) = delete;
    LoggerBase(LoggerBase&&) = delete;
    LoggerBase& operator=(LoggerBase&&) = delete;

    /**
     * Log message with source location
     *
     * Uses std::vformat (non-template) to avoid code bloat
     */
    virtual void log(LogLevel level,
                    std::string_view message,
                    const std::source_location& loc) = 0;
    
    /**
     * Check if level is enabled (for performance)
     */
    virtual bool isEnabled(LogLevel level) const = 0;
    
    /**
     * Set minimum log level
     */
    virtual void setLevel(LogLevel level) = 0;

    virtual void flush() {}
    virtual void shutdown() {}

protected:
    LoggerBase() = default;
};

namespace detail {

/// Wrapper that carries both a `std::format_string` and the call-site's
/// `std::source_location`. C++20 default arguments evaluate at the call
/// site, so declaring `loc` as a defaulted constructor parameter captures
/// the user's file/line — which is what we want in log output.
///
/// Why this helper exists: a templated log method like
///     template<typename... Args>
///     void info(std::format_string<Args...> fmt, Args&&... args,
///               std::source_location loc = std::source_location::current());
/// does *not* work — the variadic pack swallows the trailing argument, so
/// the default never fires. Wrapping the format string in a small struct
/// (deduced through `std::type_identity_t` on the Args pack so the
/// parameter does not participate in template deduction) sidesteps the
/// pack-eats-default problem and is the idiomatic C++20/23 fix.
template<typename... Args>
struct FmtWithLoc {
    template<typename S>
        requires std::convertible_to<const S&, std::format_string<Args...>>
    consteval FmtWithLoc(                                           // NOLINT
        const S& s,
        std::source_location l = std::source_location::current())
        : fmt(s), loc(l) {}

    std::format_string<Args...> fmt;
    std::source_location loc;
};

}  // namespace detail

/**
 * Logger - Facade with std::vformat (SOLID: Dependency Inversion)
 *
 * Type-safe logging API that uses std::vformat internally.
 * No code bloat - single instantiation per log call.
 * Source location is captured at the call site via detail::FmtWithLoc.
 *
 * Usage:
 *   Logger logger(std::make_unique<FileLogger>("app.log"));
 *   logger.error("Value: {} at {}", x, y);   // file/line = caller's
 */
class Logger {
public:
    explicit Logger(std::unique_ptr<LoggerBase> impl)
        : impl_(std::move(impl)) {}
    
    // Forwarding to implementation
    void setLevel(LogLevel level) {
        impl_->setLevel(level);
    }
    
    bool isEnabled(LogLevel level) const {
        return impl_->isEnabled(level);
    }
    
    /**
     * Type-safe logging with std::vformat (NO CODE BLOAT!)
     *
     * Template only in wrapper - vformat does actual work
     * Single instantiation regardless of argument types
     */
    // Public API — the `FmtWithLoc` wrapper captures the caller's
    // source_location via a default argument on its constructor. We route
    // the real work through the first-parameter-location `<level>Impl()`
    // helpers below.
    template<typename... Args>
    void trace(detail::FmtWithLoc<std::type_identity_t<Args>...> fmt, Args&&... args) {
        traceImpl(fmt.loc, fmt.fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void debug(detail::FmtWithLoc<std::type_identity_t<Args>...> fmt, Args&&... args) {
        debugImpl(fmt.loc, fmt.fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void info(detail::FmtWithLoc<std::type_identity_t<Args>...> fmt, Args&&... args) {
        infoImpl(fmt.loc, fmt.fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void warn(detail::FmtWithLoc<std::type_identity_t<Args>...> fmt, Args&&... args) {
        warnImpl(fmt.loc, fmt.fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void error(detail::FmtWithLoc<std::type_identity_t<Args>...> fmt, Args&&... args) {
        errorImpl(fmt.loc, fmt.fmt, std::forward<Args>(args)...);
    }
    template<typename... Args>
    void critical(detail::FmtWithLoc<std::type_identity_t<Args>...> fmt, Args&&... args) {
        criticalImpl(fmt.loc, fmt.fmt, std::forward<Args>(args)...);
    }

    void flush() { impl_->flush(); }
    void shutdown() { impl_->shutdown(); }

    LoggerBase* getImpl() { return impl_.get(); }

private:
    // Implementation - source_location as first parameter
    template<typename... Args>
    void traceImpl(const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args) {
        if (!impl_->isEnabled(LogLevel::TRACE)) return;
        auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
        impl_->log(LogLevel::TRACE, msg, loc);
    }

    template<typename... Args>
    void debugImpl(const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args) {
        if (!impl_->isEnabled(LogLevel::DEBUG)) return;
        auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
        impl_->log(LogLevel::DEBUG, msg, loc);
    }

    template<typename... Args>
    void infoImpl(const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args) {
        if (!impl_->isEnabled(LogLevel::INFO)) return;
        auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
        impl_->log(LogLevel::INFO, msg, loc);
    }
    
    template<typename... Args>
    void warnImpl(const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args) {
        if (!impl_->isEnabled(LogLevel::WARN)) return;
        auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
        impl_->log(LogLevel::WARN, msg, loc);
    }
    
    template<typename... Args>
    void errorImpl(const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args) {
        if (!impl_->isEnabled(LogLevel::ERROR)) return;
        auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
        impl_->log(LogLevel::ERROR, msg, loc);
    }
    
    template<typename... Args>
    void criticalImpl(const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args) {
        if (!impl_->isEnabled(LogLevel::CRITICAL)) return;
        auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
        impl_->log(LogLevel::CRITICAL, msg, loc);
    }

private:
    std::unique_ptr<LoggerBase> impl_;
};

/**
 * Helper functions
 */
inline const char* levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:    return "TRACE";
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARN:     return "WARN";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRIT";
        default:                 return "UNKN";
    }
}

inline std::string formatTimestamp() {
    constexpr int kMillisPerSecond = 1000;
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % kMillisPerSecond;

    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    // HH:MM:SS.mmm — drop the date (user can see it via file mtime / wall
    // clock) so each line in the live log panel stays readable.
    std::ostringstream oss;
    oss << std::put_time(&localTime, "%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

/// Keep only the filename from a full source_location path (strip `/` and
/// `\`). Returns the trailing component as a `string_view` into the input.
inline std::string_view shortFileName(const char* full) {
    std::string_view sv{full ? full : ""};
    if (auto pos = sv.find_last_of("/\\"); pos != std::string_view::npos) {
        sv.remove_prefix(pos + 1);
    }
    return sv;
}

/// Fixed-width level token so messages line up vertically in the log
/// panel. 5 chars covers TRACE / DEBUG / INFO / WARN / ERROR / CRIT.
inline const char* levelToPaddedString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:    return "TRACE";
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO ";
        case LogLevel::WARN:     return "WARN ";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRIT ";
        default:                 return "UNKN ";
    }
}

} // namespace app::core

#endif // LOGGER_BASE_H
