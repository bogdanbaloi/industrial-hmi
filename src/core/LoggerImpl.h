#ifndef LOGGER_IMPLEMENTATIONS_H
#define LOGGER_IMPLEMENTATIONS_H

#include "LoggerBase.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <mutex>
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
        
        std::lock_guard lock(mutex_);
        
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
    
private:
    LogLevel minLevel_;
    std::mutex mutex_;
};

/**
 * FileLogger - Output to file (SOLID: Single Responsibility)
 */
class FileLogger : public LoggerBase {
public:
    explicit FileLogger(const std::string& filename,
                       LogLevel minLevel = LogLevel::INFO)
        : minLevel_(minLevel) {

        // Create parent directories if they don't exist
        auto parentPath = std::filesystem::path(filename).parent_path();
        if (!parentPath.empty()) {
            std::filesystem::create_directories(parentPath);
        }

        file_.open(filename, std::ios::app);
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to open log file: " + filename);
        }
    }
    
    ~FileLogger() {
        if (file_.is_open()) {
            file_.close();
        }
    }
    
    void log(LogLevel level,
            std::string_view message,
            const std::source_location& loc) override {
        
        std::lock_guard lock(mutex_);
        
        if (file_.is_open()) {
            file_ << std::format("[{}] [{}] [{}:{}] {}\n",
                                formatTimestamp(),
                                levelToString(level),
                                loc.file_name(),
                                loc.line(),
                                message);
            file_.flush();
        }
    }
    
    bool isEnabled(LogLevel level) const override {
        return level >= minLevel_;
    }
    
    void setLevel(LogLevel level) override {
        minLevel_ = level;
    }
    
private:
    LogLevel minLevel_;
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

} // namespace app::core

#endif // LOGGER_IMPLEMENTATIONS_H
