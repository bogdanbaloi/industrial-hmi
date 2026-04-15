#pragma once

#include "BasePresenter.h"
#include "src/presenter/modelview/PlaceholderViewModels.h"
#include "src/model/DatabaseManager.h"
#include "src/model/ProductsRepository.h"
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
///
/// The presenter takes a ProductsRepository reference for read operations.
/// Production wiring uses DatabaseManager (the singleton implements the
/// interface); tests inject a MockProductsRepository.
class ProductsPresenter : public BasePresenter {
public:
    /// Default constructor wires up the production DatabaseManager singleton.
    ProductsPresenter();

    /// DI constructor used by tests to inject a mock repository.
    explicit ProductsPresenter(model::ProductsRepository& repository);

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

    /// Read repository (injected). Async helpers still use DatabaseManager
    /// directly; only the synchronous read paths route through here.
    model::ProductsRepository& repository_;

    /// Cached search query
    std::string currentSearchQuery_;
};

}  // namespace app
