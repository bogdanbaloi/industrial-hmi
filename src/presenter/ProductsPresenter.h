#pragma once

#include "BasePresenter.h"
#include "src/presenter/modelview/PlaceholderViewModels.h"
#include <string>

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

    /// Load all products from database
    void loadProducts();
    
    /// Search products by query string
    void searchProducts(const std::string& query);
    
    /// View product details
    void viewProduct(int productId);

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
