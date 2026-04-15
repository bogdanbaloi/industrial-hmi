// Tests for app::model::DatabaseManager
// DatabaseManager is a singleton backed by an in-memory SQLite connection.
// We initialize it once per test binary (SetUpTestSuite) and keep test
// isolation by having every mutating test use a unique product_code that no
// other test touches. Read-only tests assert against the 6-row demo seed.

#include "src/model/DatabaseManager.h"
#include "src/core/Result.h"
#include "src/core/ErrorHandling.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using app::model::DatabaseManager;

class DatabaseManagerTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // One shared in-memory DB for the whole binary; demo data loaded by
        // initialize() provides the read-only fixture (PROD-001..PROD-006).
        auto result = DatabaseManager::instance().initialize();
        ASSERT_TRUE(result.isOk())
            << "Database initialization failed: " << result.errorMessage();
    }

    // Convenience: look up a product by code in an already-loaded list.
    static const DatabaseManager::Product*
    findByCode(const std::vector<DatabaseManager::Product>& products,
               const std::string& code) {
        auto it = std::find_if(products.begin(), products.end(),
            [&](const DatabaseManager::Product& p) {
                return p.productCode == code;
            });
        return it == products.end() ? nullptr : &*it;
    }
};

// ============================================================================
// Initialize + demo seed
// ============================================================================

TEST_F(DatabaseManagerTest, InitializeSucceeds) {
    // SetUpTestSuite already ran initialize() once; calling it again should
    // still return Ok on an in-memory connection (it is idempotent enough).
    auto result = DatabaseManager::instance().initialize();
    EXPECT_TRUE(result.isOk());
}

TEST_F(DatabaseManagerTest, DemoSeedContainsSixProducts) {
    auto products = DatabaseManager::instance().getAllProducts();
    EXPECT_GE(products.size(), 6u);

    // Spot-check a few known codes from the seed data
    EXPECT_NE(findByCode(products, "PROD-001"), nullptr);
    EXPECT_NE(findByCode(products, "PROD-006"), nullptr);
}

TEST_F(DatabaseManagerTest, DemoProductAHasExpectedFields) {
    auto products = DatabaseManager::instance().getAllProducts();
    const auto* a = findByCode(products, "PROD-001");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->name, "Product A");
    EXPECT_EQ(a->status, "Active");
    EXPECT_EQ(a->stock, 850);
    EXPECT_FLOAT_EQ(a->qualityRate, 98.1f);
}

// ============================================================================
// getProduct(id)
// ============================================================================

TEST_F(DatabaseManagerTest, GetProductByIdReturnsMatchingRow) {
    auto all = DatabaseManager::instance().getAllProducts();
    ASSERT_FALSE(all.empty());
    const auto expectedId = all.front().id;
    const auto expectedCode = all.front().productCode;

    auto p = DatabaseManager::instance().getProduct(expectedId);
    EXPECT_EQ(p.id, expectedId);
    EXPECT_EQ(p.productCode, expectedCode);
}

TEST_F(DatabaseManagerTest, GetProductByIdReturnsInvalidForMissing) {
    auto p = DatabaseManager::instance().getProduct(999999);
    EXPECT_EQ(p.id, app::config::defaults::kInvalidProductId);
}

// ============================================================================
// searchProducts
// ============================================================================

TEST_F(DatabaseManagerTest, SearchByCodeFindsMatchingRow) {
    auto matches = DatabaseManager::instance().searchProducts("PROD-001");
    ASSERT_FALSE(matches.empty());
    EXPECT_EQ(matches.front().productCode, "PROD-001");
}

TEST_F(DatabaseManagerTest, SearchByNameFindsMatchingRow) {
    auto matches = DatabaseManager::instance().searchProducts("Product A");
    ASSERT_FALSE(matches.empty());
    EXPECT_EQ(matches.front().name, "Product A");
}

TEST_F(DatabaseManagerTest, SearchWithNoMatchesReturnsEmpty) {
    auto matches = DatabaseManager::instance().searchProducts(
        "__definitely_not_a_real_product_name__");
    EXPECT_TRUE(matches.empty());
}

// ============================================================================
// addProduct
// ============================================================================

TEST_F(DatabaseManagerTest, AddProductWithNewCodeSucceeds) {
    const std::string code = "TEST-ADD-001";
    EXPECT_TRUE(DatabaseManager::instance().addProduct(
        code, "Test Widget", "Active", 100, 95.5f));

    auto all = DatabaseManager::instance().getAllProducts();
    const auto* added = findByCode(all, code);
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(added->name, "Test Widget");
    EXPECT_EQ(added->status, "Active");
    EXPECT_EQ(added->stock, 100);
    EXPECT_FLOAT_EQ(added->qualityRate, 95.5f);
}

TEST_F(DatabaseManagerTest, AddProductWithDuplicateCodeFails) {
    // PROD-001 exists in the demo seed
    EXPECT_FALSE(DatabaseManager::instance().addProduct(
        "PROD-001", "Dup", "Active", 10, 90.0f));
}

// ============================================================================
// updateProduct
// ============================================================================

TEST_F(DatabaseManagerTest, UpdateProductChangesValues) {
    const std::string code = "TEST-UPDATE-001";
    ASSERT_TRUE(DatabaseManager::instance().addProduct(
        code, "Before", "Active", 50, 90.0f));

    // Find the newly inserted row so we can reference it by id
    auto all = DatabaseManager::instance().getAllProducts();
    const auto* added = findByCode(all, code);
    ASSERT_NE(added, nullptr);
    const int id = added->id;

    EXPECT_TRUE(DatabaseManager::instance().updateProduct(
        id, "After", "Inactive", 75, 80.0f));

    auto updated = DatabaseManager::instance().getProduct(id);
    EXPECT_EQ(updated.name, "After");
    EXPECT_EQ(updated.status, "Inactive");
    EXPECT_EQ(updated.stock, 75);
    EXPECT_FLOAT_EQ(updated.qualityRate, 80.0f);
}

// ============================================================================
// deleteProduct (soft delete)
// ============================================================================

TEST_F(DatabaseManagerTest, DeleteProductHidesItFromQueries) {
    const std::string code = "TEST-DELETE-001";
    ASSERT_TRUE(DatabaseManager::instance().addProduct(
        code, "Doomed", "Active", 1, 100.0f));

    auto all = DatabaseManager::instance().getAllProducts();
    const auto* added = findByCode(all, code);
    ASSERT_NE(added, nullptr);
    const int id = added->id;

    EXPECT_TRUE(DatabaseManager::instance().deleteProduct(id));

    // Soft-deleted rows must disappear from both listings and single-id lookup
    auto afterAll = DatabaseManager::instance().getAllProducts();
    EXPECT_EQ(findByCode(afterAll, code), nullptr);

    auto afterGet = DatabaseManager::instance().getProduct(id);
    EXPECT_EQ(afterGet.id, app::config::defaults::kInvalidProductId);
}
