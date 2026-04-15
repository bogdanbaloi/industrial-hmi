#pragma once

#include "src/model/Product.h"

#include <string>
#include <vector>

namespace app::model {

/// Read-side abstraction over the products table.
///
/// Lets the presenter layer depend on a small interface instead of the
/// DatabaseManager singleton, which keeps the presenter unit-testable
/// (tests inject a mock repository via constructor).
///
/// Only synchronous read operations are exposed here. Async helpers
/// (addProductAsync / updateProductAsync / deleteProductAsync) and the
/// Boost.Asio-backed I/O context they require stay on DatabaseManager and
/// are out of scope for this abstraction; they need a separate mocking
/// strategy.
class ProductsRepository {
public:
    virtual ~ProductsRepository() = default;

    [[nodiscard]] virtual std::vector<Product> getAllProducts() = 0;
    [[nodiscard]] virtual Product getProduct(int id) = 0;
    [[nodiscard]] virtual std::vector<Product>
    searchProducts(const std::string& query) = 0;
};

}  // namespace app::model
