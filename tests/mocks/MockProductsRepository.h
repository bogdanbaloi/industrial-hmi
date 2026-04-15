#pragma once

#include "src/model/ProductsRepository.h"

#include <gmock/gmock.h>

namespace app::test {

/// gmock-backed implementation of ProductsRepository for presenter tests.
///
/// Lets ProductsPresenterTest set EXPECT_CALL expectations on individual
/// methods and have them return canned data without touching SQLite.
class MockProductsRepository : public model::ProductsRepository {
public:
    MOCK_METHOD(std::vector<model::Product>, getAllProducts, (), (override));
    MOCK_METHOD(model::Product, getProduct, (int id), (override));
    MOCK_METHOD(std::vector<model::Product>, searchProducts,
                (const std::string& query), (override));
};

}  // namespace app::test
