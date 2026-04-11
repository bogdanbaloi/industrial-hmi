#ifndef ERROR_HANDLING_H
#define ERROR_HANDLING_H

#include "Result.h"
#include "Logger.h"
#include <string>
#include <source_location>
#include <exception>
#include <functional>
#include <map>

namespace app::core {

/**
 * Error Categories - Typed error codes for different subsystems
 */

enum class DatabaseError {
    ConnectionFailed,
    QueryFailed,
    ConstraintViolation,
    UniqueViolation,
    Timeout,
    DiskFull,
    PermissionDenied,
    NotFound,
    Unknown
};

enum class ValidationError {
    EmptyField,
    InvalidFormat,
    OutOfRange,
    TooLong,
    TooShort,
    InvalidCharacters
};

enum class IOError {
    ThreadFailed,
    OperationCancelled,
    QueueFull,
    Timeout
};

enum class UIError {
    WidgetNotFound,
    InvalidState,
    DialogFailed
};

/**
 * Error to string conversions (required by Result<T, E>)
 */
template<>
inline std::string Result<int, DatabaseError>::errorToString(DatabaseError error) {
    switch (error) {
        case DatabaseError::ConnectionFailed:   return "Database connection failed";
        case DatabaseError::QueryFailed:        return "Database query failed";
        case DatabaseError::ConstraintViolation: return "Database constraint violation";
        case DatabaseError::UniqueViolation:    return "Duplicate key violation";
        case DatabaseError::Timeout:            return "Database operation timeout";
        case DatabaseError::DiskFull:           return "Disk full";
        case DatabaseError::PermissionDenied:   return "Permission denied";
        case DatabaseError::NotFound:           return "Record not found";
        default:                                return "Unknown database error";
    }
}

template<>
inline std::string Result<int, ValidationError>::errorToString(ValidationError error) {
    switch (error) {
        case ValidationError::EmptyField:       return "Required field is empty";
        case ValidationError::InvalidFormat:    return "Invalid format";
        case ValidationError::OutOfRange:       return "Value out of range";
        case ValidationError::TooLong:          return "Value too long";
        case ValidationError::TooShort:         return "Value too short";
        case ValidationError::InvalidCharacters: return "Invalid characters";
        default:                                return "Unknown validation error";
    }
}

template<>
inline std::string Result<int, IOError>::errorToString(IOError error) {
    switch (error) {
        case IOError::ThreadFailed:         return "I/O thread failed";
        case IOError::OperationCancelled:   return "Operation cancelled";
        case IOError::QueueFull:            return "Operation queue full";
        case IOError::Timeout:              return "I/O operation timeout";
        default:                            return "Unknown I/O error";
    }
}

/**
 * ExceptionHandler - Global exception handling with C++20 source_location
 * 
 * Features:
 * - Automatic logging with source location
 * - Exception type categorization
 * - Recovery handlers
 * - Thread-safe exception tracking
 */
class ExceptionHandler {
public:
    using RecoveryHandler = std::function<void(const std::exception&)>;
    
    static ExceptionHandler& instance() {
        static ExceptionHandler inst;
        return inst;
    }
    
    /**
     * Handle exception with automatic logging
     * 
     * @param e Exception to handle
     * @param context Additional context
     * @param loc Source location (automatic)
     */
    void handle(const std::exception& e, 
                const std::string& context = "",
                const std::source_location& loc = std::source_location::current()) {
        
        Logger::error("Exception caught: {} | Context: {} | Location: {}:{}",
                     e.what(),
                     context,
                     loc.file_name(),
                     loc.line());
        
        // Try recovery
        auto it = recoveryHandlers_.find(typeid(e).name());
        if (it != recoveryHandlers_.end()) {
            try {
                it->second(e);
                Logger::info("Recovery handler executed successfully");
            } catch (const std::exception& recoveryErr) {
                Logger::critical("Recovery handler failed: {}", recoveryErr.what());
            }
        }
        
        ++exceptionCount_;
    }
    
    /**
     * Handle unknown exception
     */
    void handleUnknown(const std::string& context = "",
                      const std::source_location& loc = std::source_location::current()) {
        
        Logger::critical("Unknown exception caught | Context: {} | Location: {}:{}",
                        context,
                        loc.file_name(),
                        loc.line());
        
        ++unknownExceptionCount_;
    }
    
    /**
     * Register recovery handler for specific exception type
     */
    template<typename E>
    void registerRecoveryHandler(RecoveryHandler handler) {
        recoveryHandlers_[typeid(E).name()] = std::move(handler);
    }
    
    /**
     * Safe execution wrapper - catches all exceptions
     * 
     * Usage:
     *   ExceptionHandler::safeExecute([&]() {
     *       riskyOperation();
     *   }, "RiskyOperation");
     */
    template<typename F>
    static bool safeExecute(F&& func, const std::string& context = "",
                           const std::source_location& loc = std::source_location::current()) {
        try {
            func();
            return true;
        } catch (const std::exception& e) {
            instance().handle(e, context, loc);
            return false;
        } catch (...) {
            instance().handleUnknown(context, loc);
            return false;
        }
    }
    
    /**
     * Get exception statistics
     */
    size_t getExceptionCount() const { return exceptionCount_; }
    size_t getUnknownExceptionCount() const { return unknownExceptionCount_; }
    
    void resetStats() {
        exceptionCount_ = 0;
        unknownExceptionCount_ = 0;
    }
    
private:
    ExceptionHandler() = default;
    
    std::map<std::string, RecoveryHandler> recoveryHandlers_;
    std::atomic<size_t> exceptionCount_{0};
    std::atomic<size_t> unknownExceptionCount_{0};
};

/**
 * RAII Exception Guard - Ensures exception handling in scope
 */
class ExceptionGuard {
public:
    ExceptionGuard(const std::string& context,
                  const std::source_location& loc = std::source_location::current())
        : context_(context), location_(loc) {}
    
    ~ExceptionGuard() {
        if (std::uncaught_exceptions() > 0) {
            ExceptionHandler::instance().handleUnknown(
                context_ + " [uncaught during destruction]",
                location_
            );
        }
    }
    
private:
    std::string context_;
    std::source_location location_;
};

} // namespace app::core

#endif // ERROR_HANDLING_H
