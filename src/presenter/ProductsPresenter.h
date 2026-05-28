#pragma once

#include "BasePresenter.h"
#include "src/presenter/modelview/PlaceholderViewModels.h"
#include "src/model/DatabaseManager.h"
#include "src/model/ProductsRepository.h"
#include "src/model/RecipesRepository.h"
#include <string>
#include <functional>

namespace app::auth {
class AuditLogger;
class Session;
}

namespace app::model {
class ProductionModel;
}

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

    /// Export all active products asynchronously (for CSV export).
    /// Callback runs on GTK main thread with the product list.
    void exportProducts(std::function<void(std::vector<model::Product>)> callback);

    /// Audit hookup. When set, every add / update / delete records a
    /// PRODUCT category event with the operator's identity at the
    /// time of the action. Mirrors DashboardPresenter::setAudit so
    /// composition wiring stays uniform.
    void setAudit(app::auth::AuditLogger& audit,
                  app::auth::Session& session) {
        audit_   = &audit;
        session_ = &session;
    }

    /// Recipe-loading hookup. When both are set, the Products page's
    /// "Load Recipe" action can fetch a product's recipe and load it
    /// onto the production line. Optional + injected (mirrors setAudit)
    /// so the repo-only test constructor and the headless build stay
    /// unaffected. See REQ-PRODUCTS-003.
    void setRecipeLoading(model::RecipesRepository& recipes,
                          model::ProductionModel& productionModel) {
        recipes_         = &recipes;
        productionModel_ = &productionModel;
    }

    /// Look up the product's recipe and load it onto the production
    /// line, making it the active work unit. Notifies observers via
    /// onRecipeLoaded (success + message, or failure when the product
    /// has no recipe / the hookup is absent).
    void loadRecipe(int productId);

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

    /// Audit hookup -- both null when audit is disabled or this
    /// presenter is exercised in a unit test.
    app::auth::AuditLogger* audit_{nullptr};
    app::auth::Session*     session_{nullptr};

    /// Recipe-loading hookup -- both null unless setRecipeLoading was
    /// called (production wiring). loadRecipe is a no-op-with-error
    /// when absent.
    model::RecipesRepository* recipes_{nullptr};
    model::ProductionModel*   productionModel_{nullptr};
};

}  // namespace app
