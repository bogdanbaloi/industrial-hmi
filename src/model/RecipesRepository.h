#pragma once

#include "src/model/Recipe.h"

#include <optional>
#include <string>

namespace app::model {

/// Read-side abstraction over the recipes tables.
///
/// Mirrors ProductsRepository: lets the presenter layer depend on a small
/// interface instead of the DatabaseManager singleton, so it stays
/// unit-testable (tests inject a mock). Recipes are looked up by the
/// product code the operator selected on the Products page.
///
/// Returns std::optional so a product with no recipe is an explicit
/// "not found" the caller surfaces to the user, never a silent default.
class RecipesRepository {
public:
    virtual ~RecipesRepository() = default;

    [[nodiscard]] virtual std::optional<Recipe>
    getRecipeByProductCode(const std::string& productCode) = 0;
};

}  // namespace app::model
