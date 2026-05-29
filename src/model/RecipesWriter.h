#pragma once

#include "src/model/Recipe.h"

namespace app::model {

/// Write-side abstraction over the recipes tables.
///
/// Deliberately separate from RecipesRepository (read) -- same
/// Interface-Segregation split the historian uses for HistoryReader /
/// HistoryWriter. The recipe *loading* path only ever reads, so it keeps
/// depending on RecipesRepository; only the recipe *editor* needs this
/// writer. DatabaseManager implements both; a consumer depends on the
/// surface it actually uses, and read-only test doubles stay untouched.
class RecipesWriter {
public:
    virtual ~RecipesWriter() = default;

    /// Create or replace the recipe for `recipe.productCode`: upserts the
    /// operation count and replaces that product's per-checkpoint targets
    /// wholesale, in one transaction. Returns false on a storage error.
    [[nodiscard]] virtual bool upsertRecipe(const Recipe& recipe) = 0;
};

}  // namespace app::model
