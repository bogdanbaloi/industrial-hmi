#pragma once

#include "BasePresenter.h"
#include "src/presenter/modelview/PlaceholderViewModels.h"
#include "src/model/DatabaseManager.h"
#include <string>
#include <functional>

namespace app {

/// Presenter for Products page - demonstrates database integration
///
/// Shows how Presenter layer handles:
/// - Database queries
/// - Data transformation to ViewModels
/// - Search/filter operations
/// - CRUD operations (Read in this demo)
class ProductsPresenter : public BasePresenter {
public:
    ProductsPresenter() = default;
    ~ProductsPresenter() override = default;

    ProductsPresenter(const ProductsPresenter&) = delete;
    ProductsPresenter& operator=(const ProductsPresenter&) = delete;

    void initialize() override;

    /// Load all products from database (ASYNC - non-blocking)
    void loadProducts();
    
    /// Search products by query string
    void searchProducts(const std::string& query);
    
    /// View product details
    void viewProduct(int productId);
    
    /// Add new product to database (ASYNC - non-blocking)
    /// @param callback Called with success/failure result
    void addProduct(const std::string& productCode, const std::string& name,
                    const std::string& status, int stock, float qualityRate,
                    std::function<void(bool)> callback);
    
    /// Update existing product (ASYNC - non-blocking)
    /// @param callback Called with success/failure result
    void updateProduct(int productId, const std::string& name,
                      const std::string& status, int stock, float qualityRate,
                      std::function<void(bool)> callback);
    
    /// Delete product (ASYNC - non-blocking, soft delete)
    /// @param callback Called with success/failure result
    void deleteProduct(int productId, std::function<void(bool)> callback);
    
    /// Get single product (for dialogs)
    model::DatabaseManager::Product getProduct(int productId);

private:
    /// Build ProductsViewModel from database results
    presenter::ProductsViewModel buildProductsViewModel();
    
    /// Build single product detail ViewModel
    presenter::ViewProductDialogViewModel buildProductDetailViewModel(int productId);
    
    /// Notify observers of products list change
    void notifyProductsLoaded(const presenter::ProductsViewModel& vm);
    
    /// Notify observers to show product detail dialog
    void notifyViewProductReady(const presenter::ViewProductDialogViewModel& vm);
    
    /// Cached search query
    std::string currentSearchQuery_;
};

}  // namespace app
