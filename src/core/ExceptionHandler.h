#ifndef EXCEPTION_HANDLER_H
#define EXCEPTION_HANDLER_H

#include "LoggerBase.h"
#include <exception>
#include <functional>
#include <source_location>
#include <memory>
#include <map>
#include <atomic>

namespace app::core {

/**
 * ExceptionHandler - Exception handling interface (SOLID)
 * 
 * LLVM/Clang naming convention: No "I" prefix
 */
class ExceptionHandler {
public:
    using RecoveryHandler = std::function<void(const std::exception&)>;
    
    virtual ~ExceptionHandler() = default;
    
    /**
     * Handle exception
     */
    virtual void handle(const std::exception& e,
                       const std::string& context,
                       const std::source_location& loc) = 0;
    
    /**
     * Handle unknown exception
     */
    virtual void handleUnknown(const std::string& context,
                              const std::source_location& loc) = 0;
    
    /**
     * Register recovery handler
     */
    virtual void registerRecoveryHandler(const std::string& exceptionType,
                                        RecoveryHandler handler) = 0;
    
    /**
     * Get statistics
     */
    virtual size_t getExceptionCount() const = 0;
    virtual size_t getUnknownExceptionCount() const = 0;
    virtual void resetStats() = 0;
};

/**
 * StandardExceptionHandler - Concrete implementation with logging
 */
class StandardExceptionHandler : public ExceptionHandler {
public:
    explicit StandardExceptionHandler(Logger& logger)
        : logger_(logger) {}
    
    void handle(const std::exception& e,
               const std::string& context,
               const std::source_location& loc) override {
        
        logger_.error("Exception: {} | Context: {} | {}:{}",
                     e.what(), context, loc.file_name(), loc.line());
        
        // Try recovery
        auto it = recoveryHandlers_.find(typeid(e).name());
        if (it != recoveryHandlers_.end()) {
            try {
                it->second(e);
                logger_.info("Recovery handler executed");
            } catch (const std::exception& recoveryErr) {
                logger_.critical("Recovery failed: {}", recoveryErr.what());
            }
        }
        
        ++exceptionCount_;
    }
    
    void handleUnknown(const std::string& context,
                      const std::source_location& loc) override {
        
        logger_.critical("Unknown exception | Context: {} | {}:{}",
                        context, loc.file_name(), loc.line());
        ++unknownExceptionCount_;
    }
    
    void registerRecoveryHandler(const std::string& exceptionType,
                                 RecoveryHandler handler) override {
        recoveryHandlers_[exceptionType] = std::move(handler);
    }
    
    size_t getExceptionCount() const override {
        return exceptionCount_;
    }
    
    size_t getUnknownExceptionCount() const override {
        return unknownExceptionCount_;
    }
    
    void resetStats() override {
        exceptionCount_ = 0;
        unknownExceptionCount_ = 0;
    }
    
private:
    Logger& logger_;
    std::map<std::string, RecoveryHandler> recoveryHandlers_;
    std::atomic<size_t> exceptionCount_{0};
    std::atomic<size_t> unknownExceptionCount_{0};
};

/**
 * Safe execution wrapper (RAII)
 */
template<typename F>
bool safeExecute(ExceptionHandler& handler,
                F&& func,
                const std::string& context = "",
                const std::source_location& loc = std::source_location::current()) {
    try {
        func();
        return true;
    } catch (const std::exception& e) {
        handler.handle(e, context, loc);
        return false;
    } catch (...) {
        handler.handleUnknown(context, loc);
        return false;
    }
}

/**
 * ExceptionGuard - RAII exception guard
 */
class ExceptionGuard {
public:
    ExceptionGuard(ExceptionHandler& handler,
                  const std::string& context,
                  const std::source_location& loc = std::source_location::current())
        : handler_(handler), context_(context), location_(loc) {}
    
    ~ExceptionGuard() {
        if (std::uncaught_exceptions() > 0) {
            handler_.handleUnknown(context_ + " [uncaught]", location_);
        }
    }
    
private:
    ExceptionHandler& handler_;
    std::string context_;
    std::source_location location_;
};

} // namespace app::core

#endif // EXCEPTION_HANDLER_H
