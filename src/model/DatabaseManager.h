#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <sqlite3.h>
#include "src/core/Result.h"
#include "src/core/ErrorHandling.h"
#include "src/core/LoggerBase.h"
#include "src/config/config_defaults.h"
#include "src/model/Product.h"
#include "src/model/ProductsRepository.h"
#include "src/model/Recipe.h"
#include "src/model/RecipesRepository.h"
#include "src/model/RecipesWriter.h"

#include <optional>

namespace app::model {

/// Modern database manager with Result<T, E> error handling
///
/// Features:
/// - Result<T, DatabaseError> instead of bool
/// - Typed errors (ConnectionFailed, UniqueViolation, etc.)
/// - Logging integration
/// - Exception-safe
///
/// Implements ProductsRepository + RecipesRepository + RecipesWriter so
/// the presenter layer can depend on the abstractions (and tests can
/// inject mocks).
class DatabaseManager : public ProductsRepository,
                        public RecipesRepository,
                        public RecipesWriter {
public:
    // Re-export at class scope so existing call sites that say
    // `DatabaseManager::Product` keep compiling unchanged.
    using Product = ::app::model::Product;

    static DatabaseManager& instance() {
        static DatabaseManager inst;
        return inst;
    }
    
    /// Set logger (dependency injection)
    void setLogger(core::Logger& logger) {
        logger_ = &logger;
    }
    
    /// Initialize database with demo data
    [[nodiscard]] core::Result<void, core::DatabaseError> initialize() {
        // Create in-memory database for demo
        int rc = sqlite3_open(config::defaults::kDatabasePath, &db_);
        if (rc != SQLITE_OK) {
            if (logger_) {
                logger_->error("Failed to open database: {}", sqlite3_errmsg(db_));
            }
            return core::Result<void, core::DatabaseError>(core::Err, core::DatabaseError::ConnectionFailed);
        }
        
        createTables();
        populateDemoData();
        createRecipeTables();
        populateRecipeDemoData();

        if (logger_) {
            logger_->info("Database initialized successfully");
        }
        
        return core::Result<void, core::DatabaseError>(core::Ok);
    }
    
    /// Get all active products (not deleted)
    [[nodiscard]] std::vector<Product> getAllProducts() override {
        std::vector<Product> products;
        
        const char* sql = "SELECT id, product_code, name, status, stock, quality_rate, "
                         "created_at, updated_at, deleted_at "
                         "FROM products WHERE deleted_at IS NULL ORDER BY id";
        
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                products.push_back(extractProduct(stmt));
            }
            sqlite3_finalize(stmt);
        }

        return products;
    }
    
    /// Get product by ID
    [[nodiscard]] Product getProduct(int id) override {
        Product p;
        p.id = config::defaults::kInvalidProductId;
        
        const char* sql = "SELECT id, product_code, name, status, stock, quality_rate, "
                         "created_at, updated_at, deleted_at "
                         "FROM products WHERE id = ? AND deleted_at IS NULL";
        
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, id);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                p = extractProduct(stmt);
            }
            sqlite3_finalize(stmt);
        }

        return p;
    }
    
    /// Search products by name or code
    [[nodiscard]] std::vector<Product>
    searchProducts(const std::string& query) override {
        std::vector<Product> products;
        
        const char* sql = "SELECT id, product_code, name, status, stock, quality_rate, "
                         "created_at, updated_at, deleted_at "
                         "FROM products "
                         "WHERE (name LIKE ? OR product_code LIKE ?) "
                         "AND deleted_at IS NULL "
                         "ORDER BY id";
        
        std::string pattern = "%" + query + "%";
        
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
            
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                products.push_back(extractProduct(stmt));
            }
            sqlite3_finalize(stmt);
        }

        return products;
    }

    /// Look up a product's recipe (RecipesRepository). Returns nullopt
    /// when the product has no recipe row, so the caller can surface a
    /// "no recipe defined" message instead of loading a silent default.
    [[nodiscard]] std::optional<Recipe>
    getRecipeByProductCode(const std::string& productCode) override {
        Recipe recipe;
        recipe.productCode = productCode;
        bool found = false;

        const char* headSql =
            "SELECT total_operations FROM recipes WHERE product_code = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, headSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, productCode.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                recipe.totalOperations = sqlite3_column_int(stmt, 0);
                found = true;
            }
            sqlite3_finalize(stmt);
        }
        if (!found) {
            return std::nullopt;
        }

        const char* targetsSql =
            "SELECT checkpoint_name, pass_rate_target "
            "FROM recipe_checkpoint_targets WHERE product_code = ? "
            "ORDER BY checkpoint_name";
        if (sqlite3_prepare_v2(db_, targetsSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, productCode.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                CheckpointTarget t;
                t.checkpointName =
                    reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                t.passRateTarget =
                    static_cast<float>(sqlite3_column_double(stmt, 1));
                recipe.checkpointTargets.push_back(std::move(t));
            }
            sqlite3_finalize(stmt);
        }

        return recipe;
    }

    /// Create or replace a product's recipe (RecipesWriter). One
    /// transaction: upsert the operation count, then replace that
    /// product's checkpoint-target rows wholesale (DELETE + INSERT) so a
    /// removed target does not linger. Returns false on any SQLite error
    /// (the transaction is rolled back).
    [[nodiscard]] bool upsertRecipe(const Recipe& recipe) override {
        if (sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, nullptr)
            != SQLITE_OK) {
            return false;
        }

        bool ok = true;

        const char* headSql =
            "INSERT OR REPLACE INTO recipes (product_code, total_operations) "
            "VALUES (?, ?)";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, headSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, recipe.productCode.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, recipe.totalOperations);
            ok = (sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_finalize(stmt);
        } else {
            ok = false;
        }

        if (ok) {
            const char* delSql =
                "DELETE FROM recipe_checkpoint_targets WHERE product_code = ?";
            if (sqlite3_prepare_v2(db_, delSql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, recipe.productCode.c_str(), -1,
                                  SQLITE_TRANSIENT);
                ok = (sqlite3_step(stmt) == SQLITE_DONE);
                sqlite3_finalize(stmt);
            } else {
                ok = false;
            }
        }

        const char* insSql =
            "INSERT INTO recipe_checkpoint_targets "
            "(product_code, checkpoint_name, pass_rate_target) VALUES (?, ?, ?)";
        for (const auto& target : recipe.checkpointTargets) {
            if (!ok) break;
            if (sqlite3_prepare_v2(db_, insSql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, recipe.productCode.c_str(), -1,
                                  SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, target.checkpointName.c_str(), -1,
                                  SQLITE_TRANSIENT);
                sqlite3_bind_double(stmt, 3, target.passRateTarget);
                ok = (sqlite3_step(stmt) == SQLITE_DONE);
                sqlite3_finalize(stmt);
            } else {
                ok = false;
            }
        }

        sqlite3_exec(db_, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
        if (logger_) {
            logger_->info("DB: upsertRecipe({}) -> {} ({} targets)",
                          recipe.productCode, ok ? "ok" : "failed",
                          recipe.checkpointTargets.size());
        }
        return ok;
    }

    /// Add new product
    bool addProduct(const std::string& productCode, const std::string& name,
                    const std::string& status, int stock, float qualityRate) {
        if (logger_) {
            logger_->trace("DB: addProduct(code={}, name={})", productCode, name);
        }
        const char* sql = "INSERT INTO products (product_code, name, status, stock, quality_rate) "
                         "VALUES (?, ?, ?, ?, ?)";

        sqlite3_stmt* stmt;
        bool success = false;

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, productCode.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, status.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 4, stock);
            sqlite3_bind_double(stmt, 5, qualityRate);

            success = (sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_finalize(stmt);
        } else if (logger_) {
            logger_->error("DB: prepare INSERT failed: {}", sqlite3_errmsg(db_));
        }

        if (logger_ && !success) {
            logger_->warn("DB: addProduct failed for code={} (likely duplicate)", productCode);
        }
        return success;
    }

    /// Soft delete product (set deleted_at timestamp)
    bool deleteProduct(int id) {
        if (logger_) {
            logger_->trace("DB: deleteProduct(id={})", id);
        }
        const char* sql = "UPDATE products SET deleted_at = CURRENT_TIMESTAMP WHERE id = ?";

        sqlite3_stmt* stmt;
        bool success = false;

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, id);
            success = (sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_finalize(stmt);
        } else if (logger_) {
            logger_->error("DB: prepare DELETE failed: {}", sqlite3_errmsg(db_));
        }

        return success;
    }

    /// Update product
    bool updateProduct(int id, const std::string& name, const std::string& status,
                      int stock, float qualityRate) {
        if (logger_) {
            logger_->trace("DB: updateProduct(id={})", id);
        }
        const char* sql = "UPDATE products SET name = ?, status = ?, stock = ?, quality_rate = ? "
                         "WHERE id = ? AND deleted_at IS NULL";

        sqlite3_stmt* stmt;
        bool success = false;

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, status.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 3, stock);
            sqlite3_bind_double(stmt, 4, qualityRate);
            sqlite3_bind_int(stmt, 5, id);

            success = (sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_finalize(stmt);
        } else if (logger_) {
            logger_->error("DB: prepare UPDATE failed: {}", sqlite3_errmsg(db_));
        }

        return success;
    }
    
    // ASYNC I/O OPERATIONS - Non-blocking Database Access
    
    /**
     * Add product asynchronously (non-blocking)
     * 
     * @param productCode Product code (must be unique)
     * @param name Product name
     * @param status Product status
     * @param stock Stock quantity
     * @param qualityRate Quality rate (0.0-100.0)
     * @param callback Called on main thread with result (true = success)
     * 
     * Pattern:
     * 1. Post to ModelContext I/O thread
     * 2. Execute database operation (blocking on I/O thread)
     * 3. Marshal result back to GTK main thread via Glib::signal_idle()
     * 4. Invoke callback with result
     */
    void addProductAsync(
        const std::string& productCode,
        const std::string& name,
        const std::string& status,
        int stock,
        float qualityRate,
        std::function<void(bool)> callback);
    
    /**
     * Update product asynchronously (non-blocking)
     */
    void updateProductAsync(
        int id,
        const std::string& name,
        const std::string& status,
        int stock,
        float qualityRate,
        std::function<void(bool)> callback);
    
    /**
     * Delete product asynchronously (non-blocking, soft delete)
     */
    void deleteProductAsync(
        int id,
        std::function<void(bool)> callback);
    
    /**
     * Load all products asynchronously (non-blocking)
     */
    void getAllProductsAsync(
        std::function<void(std::vector<Product>)> callback);
    
    ~DatabaseManager() {
        if (db_) {
            sqlite3_close_v2(db_);
        }
    }

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    DatabaseManager(DatabaseManager&&) = delete;
    DatabaseManager& operator=(DatabaseManager&&) = delete;

private:
    DatabaseManager() = default;
    
    void createTables() {
        const char* sql = R"(
            CREATE TABLE products (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                product_code TEXT NOT NULL UNIQUE,
                name TEXT NOT NULL,
                status TEXT NOT NULL CHECK(status IN ('Active', 'Inactive', 'Low Stock')),
                stock INTEGER DEFAULT 0,
                quality_rate REAL DEFAULT 0.0,
                
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                deleted_at TIMESTAMP DEFAULT NULL
            );
            
            CREATE INDEX idx_deleted_at ON products(deleted_at);
            
            CREATE TRIGGER update_products_timestamp 
            AFTER UPDATE ON products
            BEGIN
                UPDATE products SET updated_at = CURRENT_TIMESTAMP
                WHERE id = NEW.id;
            END;
        )";
        
        char* errMsg = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            sqlite3_free(errMsg);
        }
    }

    void populateDemoData() {
        const char* sql = R"(
            INSERT INTO products (product_code, name, status, stock, quality_rate) VALUES
            ('PROD-001', 'Product A', 'Active', 850, 98.1),
            ('PROD-002', 'Product B', 'Active', 320, 95.7),
            ('PROD-003', 'Product C', 'Low Stock', 45, 93.0),
            ('PROD-004', 'Product D', 'Inactive', 0, 0.0),
            ('PROD-005', 'Product E', 'Active', 1200, 99.2),
            ('PROD-006', 'Product F', 'Active', 540, 96.8)
        )";

        char* errMsg = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            sqlite3_free(errMsg);
        }

    }

    /// Recipe schema. Normalised: one `recipes` row per product (the
    /// operation count), plus N `recipe_checkpoint_targets` rows giving
    /// each quality checkpoint its own pass-rate target. Targets are
    /// matched onto live checkpoints by name (see ProductionModel::
    /// loadProduct), so the join is by product_code + checkpoint_name.
    void createRecipeTables() {
        const char* sql = R"(
            CREATE TABLE recipes (
                product_code TEXT PRIMARY KEY,
                total_operations INTEGER NOT NULL DEFAULT 5
            );

            CREATE TABLE recipe_checkpoint_targets (
                product_code TEXT NOT NULL,
                checkpoint_name TEXT NOT NULL,
                pass_rate_target REAL NOT NULL,
                PRIMARY KEY (product_code, checkpoint_name)
            );
        )";

        char* errMsg = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            sqlite3_free(errMsg);
        }
    }

    /// Seed a recipe for each demo product. Operation counts and
    /// per-checkpoint targets vary by product so loading different
    /// products visibly changes the dashboard (progress denominator +
    /// each quality card's target line).
    void populateRecipeDemoData() {
        const char* sql = R"(
            INSERT INTO recipes (product_code, total_operations) VALUES
            ('PROD-001', 5),
            ('PROD-002', 4),
            ('PROD-003', 6),
            ('PROD-005', 5),
            ('PROD-006', 7);

            INSERT INTO recipe_checkpoint_targets (product_code, checkpoint_name, pass_rate_target) VALUES
            ('PROD-001', 'Weight Check',     99.0),
            ('PROD-001', 'Hardness Test',    97.0),
            ('PROD-001', 'Final Inspection', 95.0),
            ('PROD-002', 'Weight Check',     98.0),
            ('PROD-002', 'Hardness Test',    96.0),
            ('PROD-002', 'Final Inspection', 94.0),
            ('PROD-003', 'Weight Check',     97.5),
            ('PROD-003', 'Hardness Test',    95.0),
            ('PROD-003', 'Final Inspection', 92.0),
            ('PROD-005', 'Weight Check',     99.5),
            ('PROD-005', 'Hardness Test',    98.0),
            ('PROD-005', 'Final Inspection', 97.0),
            ('PROD-006', 'Weight Check',     98.5),
            ('PROD-006', 'Hardness Test',    96.5),
            ('PROD-006', 'Final Inspection', 95.5)
        )";

        char* errMsg = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            sqlite3_free(errMsg);
        }
    }

    Product extractProduct(sqlite3_stmt* stmt) const {
        Product p;
        p.id = sqlite3_column_int(stmt, 0);
        p.productCode = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        p.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        p.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        p.stock = sqlite3_column_int(stmt, 4);
        p.qualityRate = sqlite3_column_double(stmt, 5);

        const char* createdAt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        const char* updatedAt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        const char* deletedAt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));

        p.createdAt = createdAt ? createdAt : "";
        p.updatedAt = updatedAt ? updatedAt : "";
        p.deletedAt = deletedAt ? deletedAt : "";

        return p;
    }

    sqlite3* db_{nullptr};
    core::Logger* logger_{nullptr};
};

}  // namespace app::model
