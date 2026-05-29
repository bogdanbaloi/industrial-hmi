// [utest->req~products-004~1]
// Covers REQ-PRODUCTS-004 (create / edit a product's recipe) at the
// storage seam with a REAL DatabaseManager.
//
// ProductsPresenterTest covers saveRecipe's validation + writer routing
// with a MockRecipesWriter. This closes the loop: a recipe written
// through the real RecipesWriter::upsertRecipe must come back unchanged
// through RecipesRepository::getRecipeByProductCode, and a second upsert
// must REPLACE the prior checkpoint targets wholesale (no stale rows).

#include "src/model/DatabaseManager.h"
#include "src/model/RecipesRepository.h"
#include "src/model/RecipesWriter.h"
#include "src/model/Recipe.h"

#include <gtest/gtest.h>

namespace {

using app::model::DatabaseManager;
using app::model::Recipe;
using app::model::RecipesRepository;
using app::model::RecipesWriter;

class RecipeWriteIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // In-memory DB (":memory:") with the demo products + recipes
        // seeded by initialize() -- PROD-001 already has a 3-target recipe.
        ASSERT_TRUE(DatabaseManager::instance().initialize().isOk());
    }
};

TEST_F(RecipeWriteIntegrationTest, UpsertCreatesRecipeThatReadsBack) {
    RecipesWriter&     writer = DatabaseManager::instance();
    RecipesRepository& repo   = DatabaseManager::instance();

    // A product code with no seeded recipe.
    ASSERT_FALSE(repo.getRecipeByProductCode("PROD-NEW").has_value());

    Recipe recipe;
    recipe.productCode       = "PROD-NEW";
    recipe.totalOperations   = 8;
    recipe.checkpointTargets = {{"Weight Check", 88.0F},
                                {"Final Inspection", 77.0F}};
    ASSERT_TRUE(writer.upsertRecipe(recipe));

    const auto read = repo.getRecipeByProductCode("PROD-NEW");
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->totalOperations, 8);
    ASSERT_EQ(read->checkpointTargets.size(), 2U);
    // getRecipeByProductCode orders targets by checkpoint_name ascending,
    // so "Final Inspection" sorts before "Weight Check".
    EXPECT_EQ(read->checkpointTargets[0].checkpointName, "Final Inspection");
    EXPECT_FLOAT_EQ(read->checkpointTargets[0].passRateTarget, 77.0F);
    EXPECT_EQ(read->checkpointTargets[1].checkpointName, "Weight Check");
    EXPECT_FLOAT_EQ(read->checkpointTargets[1].passRateTarget, 88.0F);
}

TEST_F(RecipeWriteIntegrationTest, UpsertReplacesPriorTargetsWholesale) {
    RecipesWriter&     writer = DatabaseManager::instance();
    RecipesRepository& repo   = DatabaseManager::instance();

    // PROD-001 is seeded with 5 operations + 3 checkpoint targets.
    const auto before = repo.getRecipeByProductCode("PROD-001");
    ASSERT_TRUE(before.has_value());
    ASSERT_EQ(before->checkpointTargets.size(), 3U);

    // Overwrite with a smaller recipe: 2 operations, a single target.
    Recipe replacement;
    replacement.productCode       = "PROD-001";
    replacement.totalOperations   = 2;
    replacement.checkpointTargets = {{"Weight Check", 50.0F}};
    ASSERT_TRUE(writer.upsertRecipe(replacement));

    const auto after = repo.getRecipeByProductCode("PROD-001");
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->totalOperations, 2);
    // The two other seeded targets must be gone -- replaced, not merged.
    ASSERT_EQ(after->checkpointTargets.size(), 1U);
    EXPECT_EQ(after->checkpointTargets[0].checkpointName, "Weight Check");
    EXPECT_FLOAT_EQ(after->checkpointTargets[0].passRateTarget, 50.0F);
}

}  // namespace
