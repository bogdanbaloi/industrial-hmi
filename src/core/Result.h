#ifndef RESULT_H
#define RESULT_H

#include <variant>
#include <concepts>
#include <stdexcept>
#include <string>
#include <format>

namespace app::core {

/**
 * Result<T, E> - Rust-inspired error handling for C++20
 * 
 * Type-safe error handling without exceptions
 * Uses std::variant + concepts for compile-time safety
 * 
 * Usage:
 *   Result<Product, DatabaseError> getProduct(int id) {
 *       if (!connected) {
 *           return Err(DatabaseError::ConnectionFailed);
 *       }
 *       return Ok(product);
 *   }
 * 
 *   auto result = getProduct(123);
 *   if (result.isOk()) {
 *       auto product = result.unwrap();
 *   } else {
 *       Logger::error("Failed: {}", result.errorMessage());
 *   }
 */

// Concept: Error type must be enum class
template<typename E>
concept ErrorType = std::is_enum_v<E>;

// Helper tags for construction. These are Rust-inspired sentinels used at
// call sites (`Result(Ok, value)`) -- the short, prefix-free names are part of
// the public API, so we opt out of the project-wide k-prefix naming rule.
struct OkTag {};
struct ErrTag {};
constexpr OkTag  Ok{};   // NOLINT(readability-identifier-naming)
constexpr ErrTag Err{};  // NOLINT(readability-identifier-naming)

/**
 * Result<T, E> - Success (T) or Error (E)
 */
template<typename T, ErrorType E>
class Result {
public:
    // Construct success result
    Result(OkTag, T value) 
        : data_(std::move(value)) {}
    
    // Construct error result
    Result(ErrTag, E error) 
        : data_(error) {}
    
    // Check if result is Ok
    [[nodiscard]] bool isOk() const noexcept {
        return std::holds_alternative<T>(data_);
    }
    
    // Check if result is Err
    [[nodiscard]] bool isErr() const noexcept {
        return std::holds_alternative<E>(data_);
    }
    
    // Unwrap value (throws if Err)
    [[nodiscard]] T& unwrap() {
        if (isErr()) {
            throw std::runtime_error(
                std::format("Called unwrap() on Err: {}", errorMessage())
            );
        }
        return std::get<T>(data_);
    }
    
    [[nodiscard]] const T& unwrap() const {
        if (isErr()) {
            throw std::runtime_error(
                std::format("Called unwrap() on Err: {}", errorMessage())
            );
        }
        return std::get<T>(data_);
    }
    
    // Unwrap or return default
    [[nodiscard]] T unwrapOr(T defaultValue) const {
        return isOk() ? std::get<T>(data_) : std::move(defaultValue);
    }
    
    // Get error (throws if Ok)
    [[nodiscard]] E error() const {
        if (isOk()) {
            throw std::runtime_error("Called error() on Ok");
        }
        return std::get<E>(data_);
    }
    
    // Get error message. Routes through Result<int, E>::errorToString so
    // callers don't need to specialize the table for every T they use.
    [[nodiscard]] std::string errorMessage() const {
        if (isOk()) return "";
        return Result<int, E>::errorToString(std::get<E>(data_));
    }
    
    // Map: transform Ok value, propagate Err
    template<typename F>
    auto map(F&& func) -> Result<decltype(func(std::declval<T>())), E> {
        using U = decltype(func(std::declval<T>()));
        if (isOk()) {
            return Result<U, E>(Ok, func(unwrap()));
        }
        return Result<U, E>(Err, error());
    }
    
    // AndThen: chain operations
    template<typename F>
    auto andThen(F&& func) -> decltype(func(std::declval<T>())) {
        if (isOk()) {
            return func(unwrap());
        }
        return decltype(func(std::declval<T>()))(Err, error());
    }
    
    // Explicit bool conversion
    [[nodiscard]] explicit operator bool() const noexcept {
        return isOk();
    }
    
    // Convert error enum to string (specialized per error type in
    // ErrorHandling.h). Public so the Result<void, E> specialization can
    // delegate to Result<int, E>::errorToString without violating access
    // control across template instantiations.
    static std::string errorToString(E error);

private:
    std::variant<T, E> data_;
};

/**
 * Result<void, E> - Specialization for void (success with no value)
 */
template<ErrorType E>
class Result<void, E> {
public:
    Result(OkTag) : data_(std::monostate{}) {}
    Result(ErrTag, E error) : data_(error) {}
    
    [[nodiscard]] bool isOk() const noexcept {
        return std::holds_alternative<std::monostate>(data_);
    }
    
    [[nodiscard]] bool isErr() const noexcept {
        return std::holds_alternative<E>(data_);
    }
    
    void unwrap() const {
        if (isErr()) {
            throw std::runtime_error(
                std::format("Called unwrap() on Err: {}", errorMessage())
            );
        }
    }
    
    [[nodiscard]] E error() const {
        if (isOk()) {
            throw std::runtime_error("Called error() on Ok");
        }
        return std::get<E>(data_);
    }
    
    [[nodiscard]] std::string errorMessage() const {
        if (isOk()) return "";
        return Result<int, E>::errorToString(std::get<E>(data_));
    }
    
    [[nodiscard]] explicit operator bool() const noexcept {
        return isOk();
    }
    
private:
    std::variant<std::monostate, E> data_;
};

} // namespace app::core

#endif // RESULT_H
