// [utest->req~products-003~1]
// Covers REQ-PRODUCTS-003 (load product recipe onto the line).
//
// INTEGRATION test for the Phase 9 recipe path with real components:
// real DatabaseManager (in-memory, seeded recipes) read through the
// real RecipesRepository interface, loaded onto the real SimulatedModel
// via loadProduct. No mocks.
//
// ProductsPresenterTest + SimulatedModelTest cover the recipe path with
// a MockRecipesRepository / MockProductionModel. This closes the loop
// end-to-end: a recipe seeded in SQLite must come back through
// getRecipeByProductCode and actually reshape the model's work unit +
// quality-checkpoint targets.

#include "src/model/DatabaseManager.h"
#include "src/model/RecipesRepository.h"
#include "src/model/SimulatedModel.h"
#include "src/model/Product.h"

#include <gtest/gtest.h>

namespace {

using app::model::DatabaseManager;
using app::model::Product;
using app::model::RecipesRepository;
using app::model::SimulatedModel;

class RecipeLoadIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // In-memory DB (kDatabasePath == ":memory:") seeded with the
        // demo products + recipes by initialize().
        ASSERT_TRUE(DatabaseManager::instance().initialize().isOk());
        model_ = &SimulatedModel::instance();
        // Seed the 3 quality checkpoints (Weight Check / Hardness Test /
        // Final Inspection) so loadProduct has something to match its
        // recipe targets onto by name.
        model_->initializeDemoData();
    }

    SimulatedModel* model_{nullptr};
};

TEST_F(RecipeLoadIntegrationTest, SeededRecipeLoadsFromDbAndReshapesModel) {
    RecipesRepository& repo = DatabaseManager::instance();

    const auto recipe = repo.getRecipeByProductCode("PROD-001");
    ASSERT_TRUE(recipe.has_value()) << "PROD-001 recipe should be seeded";
    EXPECT_EQ(recipe->totalOperations, 5);
    ASSERT_EQ(recipe->checkpointTargets.size(), 3U);

    Product product;
    product.productCode = "PROD-001";
    product.name        = "Demo tablet";
    model_->loadProduct(product, *recipe);

    // Work unit now describes the loaded product.
    const auto workUnit = model_->getWorkUnit();
    EXPECT_EQ(workUnit.productId, "PROD-001");
    EXPECT_EQ(workUnit.totalOperations, 5);

    // Recipe targets applied by name onto the seeded checkpoints:
    //   0 = Weight Check (99.0), 1 = Hardness Test (97.0),
    //   2 = Final Inspection (95.0). See DatabaseManager seed.
    EXPECT_FLOAT_EQ(model_->getQualityCheckpoint(0).passRateTarget, 99.0F);
    EXPECT_FLOAT_EQ(model_->getQualityCheckpoint(1).passRateTarget, 97.0F);
    EXPECT_FLOAT_EQ(model_->getQualityCheckpoint(2).passRateTarget, 95.0F);
}

TEST_F(RecipeLoadIntegrationTest, ProductWithoutRecipeReturnsNullopt) {
    RecipesRepository& repo = DatabaseManager::instance();
    EXPECT_FALSE(repo.getRecipeByProductCode("NO-SUCH-CODE").has_value())
        << "a product with no recipe row must be an explicit not-found";
}

}  // namespace
