# INTEGRATION ROADMAP - Result<T, E> + Logging

## COMPLETED

### Core Infrastructure (100%)
- [x] LoggerBase interface (LLVM naming)
- [x] ConsoleLogger, FileLogger, CompositeLogger, NullLogger
- [x] std::vformat (no code bloat)
- [x] ExceptionHandler interface
- [x] StandardExceptionHandler with logger DI
- [x] Result<T, E> pattern with C++20 concepts
- [x] Error categories (DatabaseError, ValidationError, IOError, UIError)
- [x] SOLID principles (all 5)
- [x] Dependency Injection in MainWindow
- [x] ModelContext exception handling + backpressure

### DatabaseManager (20% - STARTED)
- [x] Logger injection (setLogger method)
- [x] initialize() returns Result<void, DatabaseError>
- [ ] getAllProducts() returns Result<vector<Product>, DatabaseError>
- [ ] getProduct(id) returns Result<Product, DatabaseError>
- [ ] addProduct() returns Result<Product, DatabaseError>
- [ ] updateProduct() returns Result<void, DatabaseError>
- [ ] deleteProduct() returns Result<void, DatabaseError>
- [ ] searchProducts() returns Result<vector<Product>, DatabaseError>

## TODO

### Phase 1: Complete DatabaseManager Integration
1. **Refactor query methods** → Result<T, DatabaseError>
   - getAllProducts() - handle SQLITE_ERROR, SQLITE_BUSY
   - getProduct(id) - return NotFound on missing
   - searchProducts(query) - proper error propagation

2. **Refactor mutation methods** → Result<T, DatabaseError>
   - addProduct() - catch UniqueViolation (SQLITE_CONSTRAINT)
   - updateProduct() - return NotFound if missing
   - deleteProduct() (soft delete) - return NotFound if missing

3. **Add error mapping**
   ```cpp
   DatabaseError mapSqliteError(int rc) {
       switch (rc) {
           case SQLITE_CONSTRAINT: return DatabaseError::ConstraintViolation;
           case SQLITE_BUSY: return DatabaseError::Timeout;
           case SQLITE_FULL: return DatabaseError::DiskFull;
           case SQLITE_PERM: return DatabaseError::PermissionDenied;
           default: return DatabaseError::Unknown;
       }
   }
   ```

### Phase 2: Update ProductsPresenter
1. **Handle Result<T, E> from DatabaseManager**
   ```cpp
   void addProduct(...) {
       auto result = db_.addProduct(...);
       if (result.isErr()) {
           switch (result.error()) {
               case DatabaseError::UniqueViolation:
                   dialogManager_.showError("Duplicate product code");
                   break;
               case DatabaseError::ConnectionFailed:
                   dialogManager_.showError("Database unavailable");
                   break;
               default:
                   dialogManager_.showError("Unknown error");
           }
           return;
       }
       // Success path
       auto product = result.unwrap();
       view_->addProductToList(product);
   }
   ```

2. **Update async callbacks**
   ```cpp
   ctx.post([this, result = std::move(result)]() mutable {
       Glib::signal_idle().connect_once([this, result = std::move(result)]() {
           if (result.isErr()) {
               handleError(result.error());
           } else {
               refreshProducts();
           }
       });
   });
   ```

### Phase 3: Add Retry Logic
1. **Create RetryPolicy**
   ```cpp
   template<typename F>
   auto retryWithBackoff(F&& func, int maxRetries = 3) 
       -> decltype(func()) {
       
       int attempt = 0;
       while (attempt < maxRetries) {
           auto result = func();
           if (result.isOk()) return result;
           
           if (isRetryable(result.error())) {
               auto delay = calculateBackoff(attempt);
               std::this_thread::sleep_for(delay);
               attempt++;
           } else {
               return result;  // Non-retryable error
           }
       }
       return func();  // Final attempt
   }
   ```

2. **Use in ProductsPresenter**
   ```cpp
   auto result = retryWithBackoff([&]() {
       return db_.getProduct(id);
   });
   ```

### Phase 4: Circuit Breaker Pattern
1. **Create CircuitBreaker**
   ```cpp
   enum class State { Closed, Open, HalfOpen };
   
   class CircuitBreaker {
       State state_{State::Closed};
       int failureCount_{0};
       int threshold_{5};
       
       template<typename F>
       auto execute(F&& func) -> decltype(func()) {
           if (state_ == State::Open) {
               return Err(DatabaseError::CircuitOpen);
           }
           
           auto result = func();
           if (result.isErr()) {
               onFailure();
           } else {
               onSuccess();
           }
           return result;
       }
   };
   ```

### Phase 5: Health Monitoring
1. **Add metrics to DatabaseManager**
   ```cpp
   struct Metrics {
       std::atomic<size_t> queryCount{0};
       std::atomic<size_t> errorCount{0};
       std::atomic<size_t> slowQueries{0};
       std::chrono::milliseconds avgQueryTime{0};
   };
   ```

2. **Health check endpoint**
   ```cpp
   bool DatabaseManager::isHealthy() {
       return db_ != nullptr 
           && metrics_.errorCount < 100
           && metrics_.avgQueryTime < 100ms;
   }
   ```

### Phase 6: Unit Tests (Google Test)
1. **Test LoggerBase implementations**
   ```cpp
   TEST(ConsoleLogger, LogsToStdout) {
       testing::internal::CaptureStdout();
       ConsoleLogger logger;
       logger.log(LogLevel::INFO, "test", loc);
       EXPECT_THAT(testing::internal::GetCapturedStdout(), 
                   HasSubstr("test"));
   }
   ```

2. **Test Result<T, E>**
   ```cpp
   TEST(Result, UnwrapOk) {
       Result<int, DatabaseError> result = Ok(42);
       EXPECT_EQ(result.unwrap(), 42);
   }
   
   TEST(Result, UnwrapErrThrows) {
       Result<int, DatabaseError> result = Err(DatabaseError::NotFound);
       EXPECT_THROW(result.unwrap(), std::runtime_error);
   }
   ```

3. **Test DatabaseManager with NullLogger**
   ```cpp
   TEST(DatabaseManager, AddProductSuccess) {
       auto logger = createNullLogger();
       DatabaseManager db;
       db.setLogger(Logger(std::move(logger)));
       
       auto result = db.addProduct(...);
       EXPECT_TRUE(result.isOk());
   }
   ```

## PRIORITY ORDER

1. **HIGH** - Complete DatabaseManager Result integration
   - Enables proper error handling throughout app
   - Estimated: 2-3 hours

2. **HIGH** - Update ProductsPresenter error handling
   - User-facing error messages
   - Estimated: 1-2 hours

3. **MEDIUM** - Add retry logic
   - Resilience to transient failures
   - Estimated: 1 hour

4. **MEDIUM** - Circuit breaker pattern
   - Prevent cascade failures
   - Estimated: 2 hours

5. **LOW** - Metrics and monitoring
   - Observability
   - Estimated: 1 hour

6. **LOW** - Unit tests
   - Confidence in refactoring
   - Estimated: 4-6 hours

## BENEFITS WHEN COMPLETE

- **Type-safe error handling** - Compiler enforces error checking
- **Granular error handling** - Different actions for different errors
- **Retry logic** - Automatic recovery from transient failures
- **Circuit breaker** - Prevent cascade failures
- **Observability** - Metrics and health monitoring
- **Testable** - DI enables unit testing
- **Production-ready** - All failure modes handled

## CURRENT STATE

```
Core Infrastructure:     ████████████████████ 100%
DatabaseManager:         ████░░░░░░░░░░░░░░░░  20%
Presenters:              ░░░░░░░░░░░░░░░░░░░░   0%
Retry Logic:             ░░░░░░░░░░░░░░░░░░░░   0%
Circuit Breaker:         ░░░░░░░░░░░░░░░░░░░░   0%
Monitoring:              ░░░░░░░░░░░░░░░░░░░░   0%
Unit Tests:              ░░░░░░░░░░░░░░░░░░░░   0%

Overall Progress:        ██░░░░░░░░░░░░░░░░░░  15%
```

## NEXT STEPS

Continue with Phase 1: Complete DatabaseManager integration with Result<T, E>.
