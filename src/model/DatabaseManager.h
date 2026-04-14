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

namespace app::model {

/// Modern database manager with Result<T, E> error handling
/// 
/// Features:
/// - Result<T, DatabaseError> instead of bool
/// - Typed errors (ConnectionFailed, UniqueViolation, etc.)
/// - Logging integration
/// - Exception-safe
class DatabaseManager {
public:
    struct Product {
        int id;
        std::string productCode;  // PROD-001, PROD-002, etc.
        std::string name;          // Product A, Product B, etc.
        std::string status;        // Active, Inactive, Low Stock
        int stock;
        float qualityRate;         // 0.0-100.0 (percentage)
        
        // Timestamps (ISO 8601 format)
        std::string createdAt;     // "2024-04-09T14:30:22Z"
        std::string updatedAt;     // "2024-04-09T14:30:22Z"
        std::string deletedAt;     // Empty if active, timestamp if deleted
        
        // Helper
        bool isDeleted() const {
            return !deletedAt.empty();
        }
    };
    
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
        
        if (logger_) {
            logger_->info("Database initialized successfully");
        }
        
        return core::Result<void, core::DatabaseError>(core::Ok);
    }
    
    /// Get all active products (not deleted)
    [[nodiscard]] std::vector<Product> getAllProducts() {
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
    [[nodiscard]] Product getProduct(int id) {
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
    [[nodiscard]] std::vector<Product> searchProducts(const std::string& query) {
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
    
    /// Add new product
    bool addProduct(const std::string& productCode, const std::string& name, 
                    const std::string& status, int stock, float qualityRate) {
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
        }
        
        return success;
    }
    
    /// Soft delete product (set deleted_at timestamp)
    bool deleteProduct(int id) {
        const char* sql = "UPDATE products SET deleted_at = CURRENT_TIMESTAMP WHERE id = ?";
        
        sqlite3_stmt* stmt;
        bool success = false;
        
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, id);
            success = (sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_finalize(stmt);
        }
        
        return success;
    }
    
    /// Update product
    bool updateProduct(int id, const std::string& name, const std::string& status, 
                      int stock, float qualityRate) {
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
        }
        
        return success;
    }
    
    // ========================================================================
    // ASYNC I/O OPERATIONS - Non-blocking Database Access
    // ========================================================================
    
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
            sqlite3_close(db_);
        }
    }

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
