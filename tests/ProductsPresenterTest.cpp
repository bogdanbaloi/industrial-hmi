// [utest->req~products-001~1]
// [utest->req~products-003~1]
// [utest->req~arch-001~1]
// Covers REQ-PRODUCTS-001 (product CRUD),
//        REQ-PRODUCTS-003 (load product recipe onto the line).
//
// Tests for app::ProductsPresenter
// Drives the presenter with a MockProductsRepository so we can verify how
// it routes calls (load vs search), how it transforms domain Product rows
// into ProductItem view-models, and how it handles the "not found" path
// in viewProduct.
//
// gmock matchers do most of the work; an inline RecordingObserver captures
// the ViewModels emitted via notify*.

#include "src/presenter/ProductsPresenter.h"
#include "src/presenter/ViewObserver.h"
#include "src/model/Product.h"
#include "src/model/Recipe.h"
#include "src/config/config_defaults.h"
#include "mocks/MockProductsRepository.h"
#include "mocks/MockRecipesRepository.h"
#include "mocks/MockProductionModel.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

using ::testing::_;
using ::testing::Return;
using ::testing::StrictMock;
using app::test::MockProductsRepository;
using app::ProductsPresenter;
using app::model::Product;

namespace {

// Captures the most recent ViewModels emitted by the presenter so tests
// can assert on transformation results.
class CapturingObserver : public app::ViewObserver {
public:
    std::optional<app::presenter::ProductsViewModel> products;
    std::optional<app::presenter::ViewProductDialogViewModel> productDetail;
    std::optional<bool>        recipeLoadedSuccess;
    std::optional<std::string> recipeLoadedMessage;

    void onProductsLoaded(const app::presenter::ProductsViewModel& vm) override {
        products = vm;
    }
    void onViewProductReady(const app::presenter::ViewProductDialogViewModel& vm) override {
        productDetail = vm;
    }
    void onRecipeLoaded(bool success, const std::string& message) override {
        recipeLoadedSuccess = success;
        recipeLoadedMessage = message;
    }
};

Product makeProduct(int id, const std::string& code, const std::string& name,
                    const std::string& status, int stock, float quality) {
    Product p;
    p.id = id;
    p.productCode = code;
    p.name = name;
    p.status = status;
    p.stock = stock;
    p.qualityRate = quality;
    p.createdAt = "2026-04-01T10:00:00Z";
    return p;
}

}  // namespace

// Fixture

class ProductsPresenterTest : public ::testing::Test {
protected:
    void SetUp() override {
        presenter = std::make_unique<ProductsPresenter>(repo);
        presenter->addObserver(&observer);
    }

    void TearDown() override {
        presenter->removeObserver(&observer);
    }

    MockProductsRepository repo;
    CapturingObserver observer;
    std::unique_ptr<ProductsPresenter> presenter;
};

// loadProducts - routes through getAllProducts and emits ProductsViewModel

TEST_F(ProductsPresenterTest, LoadProductsCallsGetAllAndNotifies) {
    EXPECT_CALL(repo, getAllProducts())
        .WillOnce(Return(std::vector<Product>{
            makeProduct(1, "PROD-001", "Product A", "Active", 850, 98.1f),
            makeProduct(2, "PROD-002", "Product B", "Active", 320, 95.7f),
        }));

    presenter->loadProducts();

    ASSERT_TRUE(observer.products.has_value());
    const auto& vm = *observer.products;
    ASSERT_EQ(vm.products.size(), 2u);
    EXPECT_EQ(vm.products[0].productCode, "PROD-001");
    EXPECT_EQ(vm.products[0].name, "Product A");
    EXPECT_EQ(vm.products[0].stock, 850);
    EXPECT_FLOAT_EQ(vm.products[0].qualityRate, 98.1f);
    EXPECT_EQ(vm.products[1].productCode, "PROD-002");
}

TEST_F(ProductsPresenterTest, LoadProductsEmitsEmptyVmWhenRepoReturnsNothing) {
    EXPECT_CALL(repo, getAllProducts()).WillOnce(Return(std::vector<Product>{}));

    presenter->loadProducts();

    ASSERT_TRUE(observer.products.has_value());
    EXPECT_TRUE(observer.products->products.empty());
}

// searchProducts - routes through searchProducts (not getAllProducts)

TEST_F(ProductsPresenterTest, SearchProductsForwardsQueryToRepository) {
    EXPECT_CALL(repo, searchProducts(std::string{"widget"}))
        .WillOnce(Return(std::vector<Product>{
            makeProduct(7, "PROD-007", "Widget G", "Active", 100, 95.0f),
        }));

    presenter->searchProducts("widget");

    ASSERT_TRUE(observer.products.has_value());
    ASSERT_EQ(observer.products->products.size(), 1u);
    EXPECT_EQ(observer.products->products.front().productCode, "PROD-007");
}

TEST_F(ProductsPresenterTest, SearchWithEmptyQueryFallsBackToGetAll) {
    // Empty query is treated the same as loadProducts(): the presenter
    // clears the cached query and calls getAllProducts(), not searchProducts().
    EXPECT_CALL(repo, getAllProducts())
        .WillOnce(Return(std::vector<Product>{
            makeProduct(1, "PROD-001", "Product A", "Active", 850, 98.1f),
        }));
    EXPECT_CALL(repo, searchProducts(_)).Times(0);

    presenter->searchProducts("");

    ASSERT_TRUE(observer.products.has_value());
    EXPECT_EQ(observer.products->products.size(), 1u);
}

// viewProduct - emits detail view-model or "NOT FOUND" placeholder

TEST_F(ProductsPresenterTest, ViewProductEmitsDetailVmForActiveProduct) {
    EXPECT_CALL(repo, getProduct(42))
        .WillOnce(Return(makeProduct(42, "PROD-042", "Gizmo", "Active", 5, 99.0f)));

    presenter->viewProduct(42);

    ASSERT_TRUE(observer.productDetail.has_value());
    EXPECT_EQ(observer.productDetail->productId, "PROD-042");
    EXPECT_TRUE(observer.productDetail->isVerified);  // Active -> verified
    EXPECT_THAT(observer.productDetail->description, ::testing::HasSubstr("Gizmo"));
    EXPECT_THAT(observer.productDetail->description, ::testing::HasSubstr("Active"));
}

TEST_F(ProductsPresenterTest, ViewProductMarksInactiveAsNotVerified) {
    EXPECT_CALL(repo, getProduct(42))
        .WillOnce(Return(makeProduct(42, "PROD-042", "Gizmo", "Inactive", 0, 0.0f)));

    presenter->viewProduct(42);

    ASSERT_TRUE(observer.productDetail.has_value());
    EXPECT_FALSE(observer.productDetail->isVerified);
}

TEST_F(ProductsPresenterTest, ViewProductEmitsNotFoundWhenRepoReturnsInvalidId) {
    Product missing;
    missing.id = app::config::defaults::kInvalidProductId;
    EXPECT_CALL(repo, getProduct(404)).WillOnce(Return(missing));

    presenter->viewProduct(404);

    ASSERT_TRUE(observer.productDetail.has_value());
    EXPECT_EQ(observer.productDetail->productId, "NOT FOUND");
    EXPECT_FALSE(observer.productDetail->isVerified);
}

// getProduct - simple passthrough to the repository

TEST_F(ProductsPresenterTest, GetProductPassesThroughToRepository) {
    EXPECT_CALL(repo, getProduct(11))
        .WillOnce(Return(makeProduct(11, "PROD-011", "Foo", "Active", 1, 100.0f)));

    auto p = presenter->getProduct(11);
    EXPECT_EQ(p.id, 11);
    EXPECT_EQ(p.productCode, "PROD-011");
}

// loadRecipe (REQ-PRODUCTS-003)

TEST_F(ProductsPresenterTest, LoadRecipeAppliesRecipeToModelAndNotifiesSuccess) {
    app::test::MockRecipesRepository recipes;
    app::test::MockProductionModel   prodModel;
    presenter->setRecipeLoading(recipes, prodModel);

    const auto product =
        makeProduct(7, "PROD-007", "Tablet X", "Active", 500, 97.0f);
    EXPECT_CALL(repo, getProduct(7)).WillOnce(Return(product));

    app::model::Recipe recipe;
    recipe.productCode = "PROD-007";
    recipe.totalOperations = 6;
    recipe.checkpointTargets = {{"Weight Check", 99.0f}};
    EXPECT_CALL(recipes, getRecipeByProductCode("PROD-007"))
        .WillOnce(Return(std::optional<app::model::Recipe>{recipe}));

    // The product + recipe must be forwarded to the production model.
    EXPECT_CALL(prodModel, loadProduct(_, _)).Times(1);

    presenter->loadRecipe(7);

    ASSERT_TRUE(observer.recipeLoadedSuccess.has_value());
    EXPECT_TRUE(*observer.recipeLoadedSuccess);
}

TEST_F(ProductsPresenterTest, LoadRecipeMissingRecipeNotifiesFailureAndSkipsModel) {
    app::test::MockRecipesRepository recipes;
    app::test::MockProductionModel   prodModel;
    presenter->setRecipeLoading(recipes, prodModel);

    EXPECT_CALL(repo, getProduct(4))
        .WillOnce(Return(makeProduct(4, "PROD-004", "No Recipe", "Inactive", 0, 0.0f)));
    EXPECT_CALL(recipes, getRecipeByProductCode("PROD-004"))
        .WillOnce(Return(std::nullopt));

    // No recipe -> the model must NOT be loaded.
    EXPECT_CALL(prodModel, loadProduct(_, _)).Times(0);

    presenter->loadRecipe(4);

    ASSERT_TRUE(observer.recipeLoadedSuccess.has_value());
    EXPECT_FALSE(*observer.recipeLoadedSuccess);
}

TEST_F(ProductsPresenterTest, LoadRecipeWithoutHookupNotifiesFailure) {
    // No setRecipeLoading() call -- presenter has no recipes repo / model.
    presenter->loadRecipe(1);

    ASSERT_TRUE(observer.recipeLoadedSuccess.has_value());
    EXPECT_FALSE(*observer.recipeLoadedSuccess);
}
