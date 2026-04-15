#pragma once

#include <string>

namespace app::model {

/// Domain entity representing a single product row.
///
/// Lives at namespace scope (rather than nested inside DatabaseManager) so
/// abstractions like ProductsRepository can refer to it without including
/// the full DatabaseManager header. DatabaseManager re-exports it via a
/// `using` alias so existing call sites that say DatabaseManager::Product
/// keep compiling.
struct Product {
    int id;
    std::string productCode;   ///< e.g. "PROD-001"
    std::string name;          ///< Display name
    std::string status;        ///< "Active" | "Inactive" | "Low Stock"
    int stock;
    float qualityRate;         ///< 0.0 - 100.0 (percent)

    // Timestamps (ISO 8601, e.g. "2024-04-09T14:30:22Z")
    std::string createdAt;
    std::string updatedAt;
    std::string deletedAt;     ///< Empty if active, timestamp if soft-deleted

    [[nodiscard]] bool isDeleted() const { return !deletedAt.empty(); }
};

}  // namespace app::model
