#pragma once

#include "src/presenter/modelview/PlaceholderViewModels.h"
#include <string>

namespace app::presenter {

/// Pure interface for Products view observers
/// 
/// @design Follows Interface Segregation Principle (SOLID):
///         - Products views only depend on methods they actually use
///         - Completely decoupled from Dashboard interface
///         - Small, focused interface (3 methods)
///
/// @pattern Observer pattern: Presenter notifies View through this interface
///
/// @lifetime Views must outlive their registration with Presenter
///           Call removeObserver() before view destruction
///
/// @threading All callbacks are invoked from Presenter thread
///            Views must handle thread-safety (e.g., Glib::Dispatcher for GTK)
class ProductsView {
public:
    /// Default constructor
    ProductsView() = default;
    
    /// Virtual destructor for polymorphism safety
    /// @design RAII: Ensures derived class destructors are called correctly
    virtual ~ProductsView() = default;
    
    // Rule of Five: Interfaces are non-copyable, non-movable
    // Views should have stable addresses for observer pattern
    ProductsView(const ProductsView&) = delete;
    ProductsView& operator=(const ProductsView&) = delete;
    ProductsView(ProductsView&&) = delete;
    ProductsView& operator=(ProductsView&&) = delete;
    
    /// Called when products list has been loaded from database
    /// @param viewModel Array of products with metadata (ID, name, category, etc)
    /// @threading Called from Presenter thread (may be database worker thread)
    virtual void onProductsLoaded(const ProductsViewModel& viewModel) = 0;
    
    /// Called when product details are ready for viewing
    /// @param viewModel Complete product information for read-only display
    /// @design Triggered by user selecting a product from the list
    /// @threading Called from Presenter thread
    virtual void onViewProductReady(const ViewProductDialogViewModel& viewModel) = 0;
    
    /// Called when product reset operation completes
    /// @param success True if reset succeeded, false if failed
    /// @param message Success confirmation or error description
    /// @threading Called from Presenter thread (may be database worker thread)
    virtual void onProductReset(bool success, const std::string& message) = 0;
};

}  // namespace app::presenter
