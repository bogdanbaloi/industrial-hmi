// Tests for ProductsPresenter's async CRUD paths (addProduct,
// updateProduct, deleteProduct). These route through
// DatabaseManager's async helpers, which post to the Boost.Asio I/O
// thread in ModelContext, then marshal the completion callback back
// via Glib::signal_idle. A plain synchronous test harness cannot
// observe that callback — we need a live Glib main loop to pump the
// idle queue.
//
// Strategy:
//   - Shared in-memory DB for the whole binary (SetUpTestSuite),
//     seeded with PROD-001..PROD-006.
//   - Each test builds a fresh Glib::MainLoop, posts an async op,
//     and quits the loop from the completion callback.
//   - A safety `signal_timeout` caps total wait at 5 seconds so a
//     stuck queue fails the test instead of hanging CI.

#include "src/presenter/ProductsPresenter.h"
#include "src/model/DatabaseManager.h"

#include <gtest/gtest.h>

#include <glibmm/main.h>

#include <chrono>
#include <memory>
#include <string>

using app::ProductsPresenter;
using app::model::DatabaseManager;

namespace {

/// One-shot main-loop pumper. Runs the caller-supplied setup (which
/// posts the async op and wires its callback to `loop->quit()`), then
/// iterates the loop until quit is called or the safety timeout fires.
///
/// Returns true if the loop exited via the callback, false if the
/// safety timeout tripped.
template <typename Setup>
bool pumpUntilCallback(Setup&& setup,
                      std::chrono::milliseconds timeout =
                          std::chrono::milliseconds{5000}) {
    auto loop = Glib::MainLoop::create();

    // The timed-out flag lives in a shared state so both the caller's
    // setup (which may quit early via its own callback) and the safety
    // timeout below can toggle it. Heap-allocated because the lambdas
    // outlive the stack frame if the loop somehow re-enters.
    auto timedOut = std::make_shared<bool>(false);

    // Fire the caller's setup. It captures `loop` and calls loop->quit()
    // from the async completion callback.
    setup(loop);

    // Safety net: if the completion callback never fires, escape after
    // `timeout` so the whole ctest run doesn't hang on a broken async
    // path. connect_once returns void (fire-and-forget), which is fine
    // — the timer references a shared_ptr loop that stays alive, and
    // calling quit() on an already-stopped loop is a no-op.
    Glib::signal_timeout().connect_once(
        [loop, timedOut]() {
            *timedOut = true;
            loop->quit();
        },
        static_cast<unsigned int>(timeout.count()));

    loop->run();
    return !*timedOut;
}

class ProductsPresenterAsyncTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        auto result = DatabaseManager::instance().initialize();
        ASSERT_TRUE(result.isOk())
            << "Database initialization failed: " << result.errorMessage();
    }

    /// Helper — synchronously resolve the id for a product by code.
    /// Used to verify mutations landed in the row we expected.
    static int lookupIdByCode(const std::string& code) {
        for (const auto& p : DatabaseManager::instance().getAllProducts()) {
            if (p.productCode == code) return p.id;
        }
        return 0;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// addProduct (async)
// ---------------------------------------------------------------------------

TEST_F(ProductsPresenterAsyncTest, AddProductSucceeds) {
    ProductsPresenter presenter;
    bool called = false;
    bool success = false;

    const bool resolved = pumpUntilCallback([&](auto loop) {
        presenter.addProduct(
            "ASYNC-001", "Async Added", "Active", 42, 91.0f,
            [&, loop](bool ok) {
                called  = true;
                success = ok;
                loop->quit();
            });
    });

    ASSERT_TRUE(resolved) << "addProduct callback never fired";
    EXPECT_TRUE(called);
    EXPECT_TRUE(success);
    EXPECT_NE(lookupIdByCode("ASYNC-001"), 0)
        << "Row ASYNC-001 not present after addProduct";
}

TEST_F(ProductsPresenterAsyncTest, AddProductWithDuplicateCodeFails) {
    // First insert succeeds.
    ProductsPresenter presenter;
    bool firstOk = false;
    pumpUntilCallback([&](auto loop) {
        presenter.addProduct(
            "ASYNC-DUP", "First", "Active", 1, 99.0f,
            [&, loop](bool ok) { firstOk = ok; loop->quit(); });
    });
    ASSERT_TRUE(firstOk);

    // Duplicate code must fail (UNIQUE constraint on product_code).
    bool called = false;
    bool success = true;  // flip to false from the callback
    const bool resolved = pumpUntilCallback([&](auto loop) {
        presenter.addProduct(
            "ASYNC-DUP", "Second", "Active", 1, 99.0f,
            [&, loop](bool ok) {
                called  = true;
                success = ok;
                loop->quit();
            });
    });

    ASSERT_TRUE(resolved);
    EXPECT_TRUE(called);
    EXPECT_FALSE(success) << "Duplicate productCode should have been rejected";
}

// ---------------------------------------------------------------------------
// updateProduct (async)
// ---------------------------------------------------------------------------

TEST_F(ProductsPresenterAsyncTest, UpdateProductMutatesFields) {
    ProductsPresenter presenter;

    // Seed a row we own so we don't perturb shared fixture rows.
    bool addOk = false;
    pumpUntilCallback([&](auto loop) {
        presenter.addProduct(
            "ASYNC-UPD", "Original Name", "Active", 10, 90.0f,
            [&, loop](bool ok) { addOk = ok; loop->quit(); });
    });
    ASSERT_TRUE(addOk);

    const int id = lookupIdByCode("ASYNC-UPD");
    ASSERT_NE(id, 0);

    bool updateOk = false;
    const bool resolved = pumpUntilCallback([&](auto loop) {
        presenter.updateProduct(
            id, "Updated Name", "Low Stock", 3, 72.5f,
            [&, loop](bool ok) { updateOk = ok; loop->quit(); });
    });

    ASSERT_TRUE(resolved);
    EXPECT_TRUE(updateOk);

    const auto p = DatabaseManager::instance().getProduct(id);
    EXPECT_EQ(p.name,   "Updated Name");
    EXPECT_EQ(p.status, "Low Stock");
    EXPECT_EQ(p.stock,  3);
    EXPECT_FLOAT_EQ(p.qualityRate, 72.5f);
}

// ---------------------------------------------------------------------------
// deleteProduct (async, soft delete)
// ---------------------------------------------------------------------------

TEST_F(ProductsPresenterAsyncTest, DeleteProductRemovesFromList) {
    ProductsPresenter presenter;

    bool addOk = false;
    pumpUntilCallback([&](auto loop) {
        presenter.addProduct(
            "ASYNC-DEL", "To Delete", "Active", 5, 88.0f,
            [&, loop](bool ok) { addOk = ok; loop->quit(); });
    });
    ASSERT_TRUE(addOk);

    const int id = lookupIdByCode("ASYNC-DEL");
    ASSERT_NE(id, 0);

    bool deleteOk = false;
    const bool resolved = pumpUntilCallback([&](auto loop) {
        presenter.deleteProduct(id, [&, loop](bool ok) {
            deleteOk = ok;
            loop->quit();
        });
    });

    ASSERT_TRUE(resolved);
    EXPECT_TRUE(deleteOk);
    EXPECT_EQ(lookupIdByCode("ASYNC-DEL"), 0)
        << "Soft-deleted row should not appear in getAllProducts";
}

// ---------------------------------------------------------------------------
// exportProducts (async; fetches snapshot for CSV export)
// ---------------------------------------------------------------------------

TEST_F(ProductsPresenterAsyncTest, ExportProductsReturnsFullList) {
    ProductsPresenter presenter;

    std::vector<app::model::Product> exported;
    bool called = false;

    const bool resolved = pumpUntilCallback([&](auto loop) {
        presenter.exportProducts([&, loop](std::vector<app::model::Product> list) {
            exported = std::move(list);
            called = true;
            loop->quit();
        });
    });

    ASSERT_TRUE(resolved);
    EXPECT_TRUE(called);
    // Demo seed + whatever earlier tests added (ASYNC-001, ASYNC-UPD).
    EXPECT_GE(exported.size(), 6u);
}
