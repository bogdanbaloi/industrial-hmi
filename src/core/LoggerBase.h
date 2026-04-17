#ifndef LOGGER_BASE_H
#define LOGGER_BASE_H

#include <string>
#include <format>
#include <source_location>
#include <memory>
#include <vector>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace app::core {

/**
 * Log Levels
 */
enum class LogLevel {
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

/**
 * Logger - Facade with std::vformat (SOLID: Dependency Inversion)
 * 
 * Type-safe logging API that uses std::vformat internally
 * No code bloat - single instantiation per log call
 * 
 * Usage:
 *   Logger logger(std::make_unique<FileLogger>("app.log"));
 *   logger.error("Value: {} at {}", x, y);
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
    template<typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args,
              const std::source_location& loc = std::source_location::current()) {
        if (!impl_->isEnabled(LogLevel::DEBUG)) return;
        
        // std::vformat - non-template, no code bloat!
        auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
        impl_->log(LogLevel::DEBUG, msg, loc);
    }
    
    // Public API - source_location captured automatically
    template<typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        info_impl(std::source_location::current(), fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        warn_impl(std::source_location::current(), fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) {
        error_impl(std::source_location::current(), fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void critical(std::format_string<Args...> fmt, Args&&... args) {
        critical_impl(std::source_location::current(), fmt, std::forward<Args>(args)...);
    }

    void flush() { impl_->flush(); }
    void shutdown() { impl_->shutdown(); }

    LoggerBase* getImpl() { return impl_.get(); }

private:
    // Implementation - source_location as first parameter
    template<typename... Args>
    void info_impl(const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args) {
        if (!impl_->isEnabled(LogLevel::INFO)) return;
        auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
        impl_->log(LogLevel::INFO, msg, loc);
    }
    
    template<typename... Args>
    void warn_impl(const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args) {
        if (!impl_->isEnabled(LogLevel::WARN)) return;
        auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
        impl_->log(LogLevel::WARN, msg, loc);
    }
    
    template<typename... Args>
    void error_impl(const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args) {
        if (!impl_->isEnabled(LogLevel::ERROR)) return;
        auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
        impl_->log(LogLevel::ERROR, msg, loc);
    }
    
    template<typename... Args>
    void critical_impl(const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args) {
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
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARN:     return "WARN";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRIT";
        default:                 return "UNKN";
    }
}

inline std::string formatTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

} // namespace app::core

#endif // LOGGER_BASE_H
