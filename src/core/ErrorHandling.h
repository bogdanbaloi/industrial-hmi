#ifndef ERROR_HANDLING_H
#define ERROR_HANDLING_H

#include "Result.h"
#include <string>

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

} // namespace app::core

#endif // ERROR_HANDLING_H
