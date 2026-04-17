#ifndef LOGGER_IMPLEMENTATIONS_H
#define LOGGER_IMPLEMENTATIONS_H

#include "LoggerBase.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <functional>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace app::core {

/**
 * ConsoleLogger - Output to stdout/stderr (SOLID: Single Responsibility)
 */
class ConsoleLogger : public LoggerBase {
public:
    explicit ConsoleLogger(LogLevel minLevel = LogLevel::INFO)
        : minLevel_(minLevel) {}
    
    void log(LogLevel level,
            std::string_view message,
            const std::source_location& loc) override {

        const std::scoped_lock lock(mutex_);

        auto& stream = (level >= LogLevel::ERROR) ? std::cerr : std::cout;
        
        stream << std::format("[{}] [{}] [{}:{}] {}\n",
                             formatTimestamp(),
                             levelToString(level),
                             loc.file_name(),
                             loc.line(),
                             message);
    }
    
    bool isEnabled(LogLevel level) const override {
        return level >= minLevel_;
    }
    
    void setLevel(LogLevel level) override {
        minLevel_ = level;
    }

    void flush() override {
        const std::scoped_lock lock(mutex_);
        std::cout.flush();
        std::cerr.flush();
    }

private:
    LogLevel minLevel_;
    std::mutex mutex_;
};

/**
 * FileLogger - Output to file (SOLID: Single Responsibility)
 */
class FileLogger : public LoggerBase {
public:
    static constexpr std::size_t kDefaultMaxFileSize = 5 * 1024 * 1024;  // 5 MB
    static constexpr int kDefaultMaxFiles = 3;

    explicit FileLogger(const std::string& filename,
                       LogLevel minLevel = LogLevel::INFO,
                       std::size_t maxFileSize = kDefaultMaxFileSize,
                       int maxFiles = kDefaultMaxFiles)
        : minLevel_(minLevel),
          filePath_(filename),
          maxFileSize_(maxFileSize),
          maxFiles_(maxFiles) {

        auto parentPath = filePath_.parent_path();
        if (!parentPath.empty()) {
            std::filesystem::create_directories(parentPath);
        }

        file_.open(filename, std::ios::app);
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to open log file: " + filename);
        }

        currentSize_ = std::filesystem::file_size(filePath_);
    }

    ~FileLogger() {
        const std::scoped_lock lock(mutex_);
        if (file_.is_open()) {
            file_.close();
        }
    }

    void log(LogLevel level,
            std::string_view message,
            const std::source_location& loc) override {

        const std::scoped_lock lock(mutex_);

        if (!file_.is_open()) return;

        auto line = std::format("[{}] [{}] [{}:{}] {}\n",
                                formatTimestamp(),
                                levelToString(level),
                                loc.file_name(),
                                loc.line(),
                                message);
        file_ << line;
        file_.flush();
        currentSize_ += line.size();

        if (maxFileSize_ > 0 && currentSize_ >= maxFileSize_) {
            rotate();
        }
    }

    bool isEnabled(LogLevel level) const override {
        return level >= minLevel_;
    }

    void setLevel(LogLevel level) override {
        minLevel_ = level;
    }

    void flush() override {
        const std::scoped_lock lock(mutex_);
        if (file_.is_open()) file_.flush();
    }

    void shutdown() override {
        const std::scoped_lock lock(mutex_);
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
    }

private:
    void rotate() {
        file_.close();

        auto stem = filePath_.stem().string();
        auto ext = filePath_.extension().string();
        auto dir = filePath_.parent_path();

        // Delete oldest rotated file
        auto oldest = dir / (stem + "." + std::to_string(maxFiles_) + ext);
        std::filesystem::remove(oldest);

        // Shift existing rotated files: app.2.log -> app.3.log, etc.
        for (int i = maxFiles_ - 1; i >= 1; --i) {
            auto src = dir / (stem + "." + std::to_string(i) + ext);
            auto dst = dir / (stem + "." + std::to_string(i + 1) + ext);
            if (std::filesystem::exists(src)) {
                std::filesystem::rename(src, dst);
            }
        }

        // Rename current file to .1
        auto first = dir / (stem + ".1" + ext);
        std::filesystem::rename(filePath_, first);

        // Reopen fresh file
        file_.open(filePath_, std::ios::trunc);
        currentSize_ = 0;
    }

    LogLevel minLevel_;
    std::filesystem::path filePath_;
    std::size_t maxFileSize_;
    int maxFiles_;
    std::size_t currentSize_{0};
    std::ofstream file_;
    std::mutex mutex_;
};

/**
 * CompositeLogger - Multiple loggers (SOLID: Open/Closed)
 * 
 * Example: Log to both console AND file
 */
class CompositeLogger : public LoggerBase {
public:
    void addLogger(std::unique_ptr<LoggerBase> logger) {
        loggers_.push_back(std::move(logger));
    }

    void removeLastLogger() {
        if (!loggers_.empty()) {
            loggers_.pop_back();
        }
    }
    
    void log(LogLevel level,
            std::string_view message,
            const std::source_location& loc) override {
        
        for (auto& logger : loggers_) {
            logger->log(level, message, loc);
        }
    }
    
    bool isEnabled(LogLevel level) const override {
        // Enabled if ANY logger is enabled
        for (const auto& logger : loggers_) {
            if (logger->isEnabled(level)) {
                return true;
            }
        }
        return false;
    }
    
    void setLevel(LogLevel level) override {
        for (auto& logger : loggers_) {
            logger->setLevel(level);
        }
    }

    void flush() override {
        for (auto& logger : loggers_) {
            logger->flush();
        }
    }

    void shutdown() override {
        for (auto& logger : loggers_) {
            logger->shutdown();
        }
    }

private:
    std::vector<std::unique_ptr<LoggerBase>> loggers_;
};

/**
 * NullLogger - No-op logger for testing (SOLID: Liskov Substitution)
 */
class NullLogger : public LoggerBase {
public:
    void log(LogLevel, std::string_view, const std::source_location&) override {
        // No-op
    }
    
    bool isEnabled(LogLevel) const override {
        return false;
    }
    
    void setLevel(LogLevel) override {
        // No-op
    }
};

/**
 * CallbackLogger - Forwards log messages to a callback function
 * Used for UI log panels that need real-time log output
 */
class CallbackLogger : public LoggerBase {
public:
    using LogCallback = std::function<void(const std::string&)>;

    explicit CallbackLogger(LogCallback callback, LogLevel minLevel = LogLevel::DEBUG)
        : callback_(std::move(callback)), minLevel_(minLevel) {}

    void log(LogLevel level,
            std::string_view message,
            const std::source_location& loc) override {
        if (!callback_ || writing_) return;
        writing_ = true;

        auto line = std::format("[{}] [{}] {}",
                                formatTimestamp(),
                                levelToString(level),
                                message);
        callback_(line);
        writing_ = false;
    }

    bool isEnabled(LogLevel level) const override {
        return level >= minLevel_;
    }

    void setLevel(LogLevel level) override {
        minLevel_ = level;
    }

private:
    LogCallback callback_;
    LogLevel minLevel_;
    std::atomic<bool> writing_{false};
};

/**
 * Parse log level from string (case-sensitive)
 */
inline LogLevel parseLogLevel(std::string_view str) {
    if (str == "DEBUG") return LogLevel::DEBUG;
    if (str == "WARN") return LogLevel::WARN;
    if (str == "ERROR") return LogLevel::ERROR;
    if (str == "CRITICAL") return LogLevel::CRITICAL;
    return LogLevel::INFO;
}

/**
 * Factory functions (SOLID: Dependency Inversion)
 */
inline std::unique_ptr<LoggerBase> createConsoleLogger(LogLevel level = LogLevel::INFO) {
    return std::make_unique<ConsoleLogger>(level);
}

inline std::unique_ptr<LoggerBase> createFileLogger(const std::string& filename,
                                                 LogLevel level = LogLevel::INFO) {
    return std::make_unique<FileLogger>(filename, level);
}

inline std::unique_ptr<LoggerBase> createCompositeLogger() {
    return std::make_unique<CompositeLogger>();
}

inline std::unique_ptr<LoggerBase> createNullLogger() {
    return std::make_unique<NullLogger>();
}

/**
 * Create typical production logger: Console + File
 */
inline std::unique_ptr<LoggerBase> createProductionLogger(const std::string& filename) {
    auto composite = std::make_unique<CompositeLogger>();
    composite->addLogger(createConsoleLogger(LogLevel::INFO));
    composite->addLogger(createFileLogger(filename, LogLevel::DEBUG));
    return composite;
}

/**
 * Create logger configured from ConfigManager settings
 */
inline std::unique_ptr<LoggerBase> createConfiguredLogger(
        const std::string& logFile,
        const std::string& levelStr,
        std::size_t maxFileSize,
        int maxFiles,
        bool consoleEnabled) {

    auto level = parseLogLevel(levelStr);
    auto composite = std::make_unique<CompositeLogger>();

    if (consoleEnabled) {
        composite->addLogger(createConsoleLogger(level));
    }

    composite->addLogger(
        std::make_unique<FileLogger>(logFile, LogLevel::DEBUG, maxFileSize, maxFiles));

    return composite;
}

} // namespace app::core

#endif // LOGGER_IMPLEMENTATIONS_H
