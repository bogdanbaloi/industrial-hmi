#pragma once

#include "src/model/RecipesRepository.h"

#include <gmock/gmock.h>

namespace app::test {

/// gmock-backed implementation of RecipesRepository for presenter tests.
/// Lets ProductsPresenterTest stub getRecipeByProductCode with a canned
/// recipe (or std::nullopt for the "no recipe defined" path) without
/// touching SQLite.
class MockRecipesRepository : public model::RecipesRepository {
public:
    MOCK_METHOD(std::optional<model::Recipe>, getRecipeByProductCode,
                (const std::string& productCode), (override));
};

}  // namespace app::test
