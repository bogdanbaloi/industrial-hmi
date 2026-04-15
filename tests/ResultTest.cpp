// Tests for app::core::Result<T, E>
// Covers Ok/Err construction, state inspection, unwrap semantics,
// chaining via map/andThen, and errorMessage text mapping.

#include "src/core/Result.h"
#include "src/core/ErrorHandling.h"

#include <gtest/gtest.h>
#include <string>

using app::core::Result;
using app::core::Ok;
using app::core::Err;
using app::core::DatabaseError;
using app::core::ValidationError;

// ============================================================================
// Construction + state inspection
// ============================================================================

TEST(ResultTest, OkIntIsOk) {
    Result<int, DatabaseError> r(Ok, 42);
    EXPECT_TRUE(r.isOk());
    EXPECT_FALSE(r.isErr());
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ResultTest, ErrIsNotOk) {
    Result<int, DatabaseError> r(Err, DatabaseError::ConnectionFailed);
    EXPECT_FALSE(r.isOk());
    EXPECT_TRUE(r.isErr());
    EXPECT_FALSE(static_cast<bool>(r));
}

TEST(ResultTest, VoidOkIsOk) {
    Result<void, DatabaseError> r(Ok);
    EXPECT_TRUE(r.isOk());
    EXPECT_FALSE(r.isErr());
}

TEST(ResultTest, VoidErrIsErr) {
    Result<void, DatabaseError> r(Err, DatabaseError::QueryFailed);
    EXPECT_FALSE(r.isOk());
    EXPECT_TRUE(r.isErr());
}

// ============================================================================
// unwrap / unwrapOr
// ============================================================================

TEST(ResultTest, UnwrapOkReturnsValue) {
    Result<int, DatabaseError> r(Ok, 7);
    EXPECT_EQ(r.unwrap(), 7);
}

TEST(ResultTest, UnwrapErrThrows) {
    Result<int, DatabaseError> r(Err, DatabaseError::Timeout);
    EXPECT_THROW((void)r.unwrap(), std::runtime_error);
}

TEST(ResultTest, UnwrapOrOkReturnsValue) {
    Result<int, DatabaseError> r(Ok, 5);
    EXPECT_EQ(r.unwrapOr(99), 5);
}

TEST(ResultTest, UnwrapOrErrReturnsDefault) {
    Result<int, DatabaseError> r(Err, DatabaseError::NotFound);
    EXPECT_EQ(r.unwrapOr(99), 99);
}

TEST(ResultTest, VoidUnwrapOkDoesNotThrow) {
    Result<void, DatabaseError> r(Ok);
    EXPECT_NO_THROW(r.unwrap());
}

TEST(ResultTest, VoidUnwrapErrThrows) {
    Result<void, DatabaseError> r(Err, DatabaseError::DiskFull);
    EXPECT_THROW(r.unwrap(), std::runtime_error);
}

// ============================================================================
// error() - extracting the error enum
// ============================================================================

TEST(ResultTest, ErrorReturnsEnumValueOnErr) {
    Result<int, DatabaseError> r(Err, DatabaseError::UniqueViolation);
    EXPECT_EQ(r.error(), DatabaseError::UniqueViolation);
}

TEST(ResultTest, ErrorOnOkThrows) {
    Result<int, DatabaseError> r(Ok, 1);
    EXPECT_THROW((void)r.error(), std::runtime_error);
}

// ============================================================================
// errorMessage() - mapping enum to human-readable string
// ============================================================================

TEST(ResultTest, ErrorMessageIsEmptyOnOk) {
    Result<int, DatabaseError> r(Ok, 1);
    EXPECT_EQ(r.errorMessage(), "");
}

TEST(ResultTest, ErrorMessageMapsDatabaseErrors) {
    // Template-comma inside the macro confuses the preprocessor, so we
    // bind each Result to a named variable first.
    Result<int, DatabaseError> connErr(Err, DatabaseError::ConnectionFailed);
    Result<int, DatabaseError> dupErr(Err, DatabaseError::UniqueViolation);
    Result<int, DatabaseError> notFound(Err, DatabaseError::NotFound);

    EXPECT_EQ(connErr.errorMessage(),  "Database connection failed");
    EXPECT_EQ(dupErr.errorMessage(),   "Duplicate key violation");
    EXPECT_EQ(notFound.errorMessage(), "Record not found");
}

TEST(ResultTest, ErrorMessageMapsValidationErrors) {
    Result<int, ValidationError> empty(Err, ValidationError::EmptyField);
    Result<int, ValidationError> oor(Err, ValidationError::OutOfRange);

    EXPECT_EQ(empty.errorMessage(), "Required field is empty");
    EXPECT_EQ(oor.errorMessage(),   "Value out of range");
}

TEST(ResultTest, VoidErrorMessageDelegatesToIntSpecialization) {
    // Result<void, E>::errorMessage forwards to Result<int, E>::errorToString
    Result<void, DatabaseError> r(Err, DatabaseError::PermissionDenied);
    EXPECT_EQ(r.errorMessage(), "Permission denied");
}

// ============================================================================
// map - transform Ok values, pass through Err unchanged
// ============================================================================

TEST(ResultTest, MapAppliesFunctionOnOk) {
    Result<int, DatabaseError> r(Ok, 10);
    auto mapped = r.map([](int x) { return x * 2; });
    ASSERT_TRUE(mapped.isOk());
    EXPECT_EQ(mapped.unwrap(), 20);
}

TEST(ResultTest, MapCanChangeType) {
    Result<int, DatabaseError> r(Ok, 42);
    auto mapped = r.map([](int x) { return std::to_string(x); });
    ASSERT_TRUE(mapped.isOk());
    EXPECT_EQ(mapped.unwrap(), "42");
}

TEST(ResultTest, MapPropagatesErrWithoutCallingFunction) {
    Result<int, DatabaseError> r(Err, DatabaseError::Timeout);
    bool called = false;
    auto mapped = r.map([&](int x) { called = true; return x * 2; });
    EXPECT_TRUE(mapped.isErr());
    EXPECT_EQ(mapped.error(), DatabaseError::Timeout);
    EXPECT_FALSE(called);
}

// ============================================================================
// andThen - chain operations that may themselves return Result
// ============================================================================

TEST(ResultTest, AndThenChainsOnOk) {
    Result<int, DatabaseError> r(Ok, 5);
    auto chained = r.andThen([](int x) {
        return Result<int, DatabaseError>(Ok, x + 1);
    });
    ASSERT_TRUE(chained.isOk());
    EXPECT_EQ(chained.unwrap(), 6);
}

TEST(ResultTest, AndThenShortCircuitsOnErr) {
    Result<int, DatabaseError> r(Err, DatabaseError::ConnectionFailed);
    bool called = false;
    auto chained = r.andThen([&](int x) {
        called = true;
        return Result<int, DatabaseError>(Ok, x);
    });
    EXPECT_TRUE(chained.isErr());
    EXPECT_EQ(chained.error(), DatabaseError::ConnectionFailed);
    EXPECT_FALSE(called);
}

TEST(ResultTest, AndThenCanReturnErrFromChain) {
    // A successful first step can feed into a fallible second step
    Result<int, DatabaseError> r(Ok, 1);
    auto chained = r.andThen([](int) {
        return Result<int, DatabaseError>(Err, DatabaseError::QueryFailed);
    });
    EXPECT_TRUE(chained.isErr());
    EXPECT_EQ(chained.error(), DatabaseError::QueryFailed);
}
