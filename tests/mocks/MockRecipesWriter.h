#pragma once

#include "src/model/RecipesWriter.h"

#include <gmock/gmock.h>

namespace app::test {

/// gmock-backed implementation of RecipesWriter for presenter tests.
/// Lets ProductsPresenterTest assert that saveRecipe forwards a validated
/// recipe to the writer (and stub success/failure) without touching
/// SQLite.
class MockRecipesWriter : public model::RecipesWriter {
public:
    MOCK_METHOD(bool, upsertRecipe, (const model::Recipe& recipe), (override));
};

}  // namespace app::test
