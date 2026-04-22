#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace app::core {

/// Classification of fatal startup failures.
///
/// Each code corresponds to a specific deployment pathology the operator
/// needs to fix before the app can run. The main() exit code and the
/// diagnostic dialog both derive from this enum, so adding a new code
/// updates every surface in one place.
enum class StartupErrorCode {
    ConfigMissing,     ///< app-config.json not found at expected path
    ConfigCorrupt,     ///< app-config.json present but cannot be parsed
    DatabaseInit,      ///< SQLite initialisation refused
    LoggerBootstrap,   ///< even the stderr logger could not be constructed
};

/// Human-readable short tag for a StartupErrorCode (used as dialog title
/// / log prefix). Kept in one place so localisation hooks can be added
/// later without rewriting main().
inline std::string_view toTag(StartupErrorCode code) {
    switch (code) {
    case StartupErrorCode::ConfigMissing:    return "CONFIG MISSING";
    case StartupErrorCode::ConfigCorrupt:    return "CONFIG CORRUPT";
    case StartupErrorCode::DatabaseInit:     return "DATABASE ERROR";
    case StartupErrorCode::LoggerBootstrap:  return "LOGGER ERROR";
    }
    return "STARTUP ERROR";
}

/// Base class for every fatal condition detected during startup.
///
/// Thrown from Bootstrap / Application / InitConsole when the app
/// cannot proceed. Caught at the top of `main()`, mapped to a native
/// dialog (MessageBoxW on Windows) or a structured stderr line (console
/// / Linux), and surfaced via a non-zero process exit code.
///
/// Design note: we keep the plain std::runtime_error hierarchy (rather
/// than introducing a std::error_code-based API) because this is a
/// one-shot startup diagnostic, not a rich error-propagation channel.
/// Operational errors use Result<T, E> (see ErrorHandling.h); runtime
/// faults use StandardExceptionHandler (see ExceptionHandler.h).
class CriticalStartupError : public std::runtime_error {
public:
    CriticalStartupError(StartupErrorCode code, std::string detail)
        : std::runtime_error(std::move(detail)), code_{code} {}

    [[nodiscard]] StartupErrorCode code() const noexcept { return code_; }

private:
    StartupErrorCode code_;
};

// Concrete subclasses — mostly for documentation / grep-ability.
// Callers can `throw ConfigMissingError{...}` for readability or fall
// back to `throw CriticalStartupError{StartupErrorCode::ConfigMissing, ...}`.

class ConfigMissingError final : public CriticalStartupError {
public:
    explicit ConfigMissingError(std::string detail)
        : CriticalStartupError(StartupErrorCode::ConfigMissing, std::move(detail)) {}
};

class ConfigCorruptError final : public CriticalStartupError {
public:
    explicit ConfigCorruptError(std::string detail)
        : CriticalStartupError(StartupErrorCode::ConfigCorrupt, std::move(detail)) {}
};

class DatabaseInitError final : public CriticalStartupError {
public:
    explicit DatabaseInitError(std::string detail)
        : CriticalStartupError(StartupErrorCode::DatabaseInit, std::move(detail)) {}
};

class LoggerBootstrapError final : public CriticalStartupError {
public:
    explicit LoggerBootstrapError(std::string detail)
        : CriticalStartupError(StartupErrorCode::LoggerBootstrap, std::move(detail)) {}
};

}  // namespace app::core
