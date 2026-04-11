#pragma once

#include <string>
#include <vector>
#include <memory>
#include <sqlite3.h>

namespace app::model {

/// Simple database manager for demo purposes
/// 
/// Demonstrates database integration patterns without exposing
/// proprietary business logic or actual production schema.
class DatabaseManager {
public:
    struct Product {
        int id;
        std::string productId;
        std::string name;
        std::string description;
        int operationsCount;
        bool isActive;
        std::string createdDate;
    };
    
    static DatabaseManager& instance() {
        static DatabaseManager inst;
        return inst;
    }
    
    /// Initialize database with demo data
    bool initialize() {
        // Create in-memory database for demo
        int rc = sqlite3_open(":memory:", &db_);
        if (rc != SQLITE_OK) {
            return false;
        }
        
        createTables();
        populateDemoData();
        return true;
    }
    
    /// Get all products
    std::vector<Product> getAllProducts() {
        std::vector<Product> products;
        
        const char* sql = "SELECT id, productId, name, description, operationsCount, "
                         "isActive, createdDate FROM products ORDER BY id";
        
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                Product p;
                p.id = sqlite3_column_int(stmt, 0);
                p.productId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                p.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                p.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                p.operationsCount = sqlite3_column_int(stmt, 4);
                p.isActive = sqlite3_column_int(stmt, 5) != 0;
                p.createdDate = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
                products.push_back(p);
            }
            sqlite3_finalize(stmt);
        }
        
        return products;
    }
    
    /// Get product by ID
    Product getProduct(int id) {
        Product p;
        
        const char* sql = "SELECT id, productId, name, description, operationsCount, "
                         "isActive, createdDate FROM products WHERE id = ?";
        
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, id);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                p.id = sqlite3_column_int(stmt, 0);
                p.productId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                p.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                p.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                p.operationsCount = sqlite3_column_int(stmt, 4);
                p.isActive = sqlite3_column_int(stmt, 5) != 0;
                p.createdDate = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            }
            sqlite3_finalize(stmt);
        }
        
        return p;
    }
    
    /// Search products by name
    std::vector<Product> searchProducts(const std::string& query) {
        std::vector<Product> products;
        
        const char* sql = "SELECT id, productId, name, description, operationsCount, "
                         "isActive, createdDate FROM products "
                         "WHERE name LIKE ? OR productId LIKE ? ORDER BY id";
        
        std::string pattern = "%" + query + "%";
        
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
            
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                Product p;
                p.id = sqlite3_column_int(stmt, 0);
                p.productId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                p.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                p.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                p.operationsCount = sqlite3_column_int(stmt, 4);
                p.isActive = sqlite3_column_int(stmt, 5) != 0;
                p.createdDate = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
                products.push_back(p);
            }
            sqlite3_finalize(stmt);
        }
        
        return products;
    }
    
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
                productId TEXT NOT NULL UNIQUE,
                name TEXT NOT NULL,
                description TEXT,
                operationsCount INTEGER DEFAULT 5,
                isActive INTEGER DEFAULT 1,
                createdDate TEXT
            )
        )";
        
        char* errMsg;
        sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);
    }
    
    void populateDemoData() {
        const char* sql = R"(
            INSERT INTO products (productId, name, description, operationsCount, isActive, createdDate) VALUES
            ('PROD-STD-001', 'Standard Item Type A', 'General purpose production item with 5 standard operations', 5, 1, '2024-01-15'),
            ('PROD-STD-002', 'Standard Item Type B', 'General purpose production item with enhanced specifications', 5, 1, '2024-01-20'),
            ('PROD-ADV-001', 'Advanced Item Type A', 'High-precision item requiring 8 specialized operations', 8, 1, '2024-02-01'),
            ('PROD-ADV-002', 'Advanced Item Type B', 'Complex multi-stage production item', 8, 1, '2024-02-10'),
            ('PROD-ECO-001', 'Economy Item', 'Simplified production item with 3 basic operations', 3, 1, '2024-01-10'),
            ('PROD-ECO-002', 'Economy Item Plus', 'Enhanced economy variant with improved specifications', 4, 1, '2024-01-25'),
            ('PROD-SPC-001', 'Specialty Item Custom', 'Custom-configured specialty production item', 10, 1, '2024-03-01'),
            ('PROD-TST-001', 'Test Item Alpha', 'Test configuration for validation purposes', 5, 0, '2024-02-15'),
            ('PROD-TST-002', 'Test Item Beta', 'Experimental configuration - inactive', 6, 0, '2024-03-05')
        )";
        
        char* errMsg;
        sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);
    }
    
    sqlite3* db_{nullptr};
};

}  // namespace app::model
