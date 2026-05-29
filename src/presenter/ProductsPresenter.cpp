#include "ProductsPresenter.h"
#include "src/auth/AuditEvent.h"
#include "src/auth/AuditLogger.h"
#include "src/auth/Session.h"
#include "src/auth/User.h"
#include "src/model/DatabaseManager.h"
#include "src/model/ProductionModel.h"
#include "src/config/config_defaults.h"
#include "src/core/i18n.h"

#include <format>

// See DashboardPresenter.cpp -- nullable logger_ inherited from BasePresenter.
#define LOG_IF(LEVEL, ...) do { if (logger_) logger_->LEVEL(__VA_ARGS__); } while (0)

namespace app {

namespace {

// Same shape as the DashboardPresenter emitAudit helper. Kept local
// rather than shared so each presenter remains free of cross-includes
// it would otherwise need just for one tiny utility.
void emitAudit(app::auth::AuditLogger* audit,
               app::auth::Session* session,
               std::string_view action,
               std::string_view details,
               std::string_view outcome) {
    if (audit == nullptr || session == nullptr) return;
    const auto userOpt = session->currentUser();
    const auto user    = userOpt.value_or(app::auth::User{});
    app::auth::AuditEvent e;
    e.username = user.username;
    e.role     = app::auth::roleName(user.role);
    e.category = app::auth::category::kProduct;
    e.action   = action;
    e.details  = details;
    e.result   = outcome;
    audit->record(e);
}

// Defense-in-depth role gate for product CRUD. Mirror of the
// DashboardPresenter helper -- a refusal records an audit FAILURE
// row + returns false. Pass-through when session is null
// (auth-disabled / test path).
bool checkEditPermission(app::auth::AuditLogger* audit,
                         app::auth::Session* session,
                         app::core::Logger* logger,
                         std::string_view action,
                         std::string_view details) {
    if (session == nullptr) return true;
    const auto userOpt = session->currentUser();
    if (!userOpt.has_value()) return false;
    if (app::auth::canEditProducts(userOpt->role)) return true;
    if (logger != nullptr) {
        logger->warn("ProductsPresenter: action {} refused -- role {} "
                     "lacks permission",
                     action, app::auth::roleName(userOpt->role));
    }
    emitAudit(audit, session, action, details,
              app::auth::result::kFailure);
    return false;
}

// Recipe validation bounds. A work unit needs at least one operation;
// the upper bound is a sanity cap so a fat-fingered spinner value can't
// store an absurd batch size. Pass-rate targets are percentages.
constexpr int   kMinOperations = 1;
constexpr int   kMaxOperations = 100;
constexpr float kMinPassRate   = 0.0F;
constexpr float kMaxPassRate   = 100.0F;

}  // namespace

ProductsPresenter::ProductsPresenter()
    : ProductsPresenter(model::DatabaseManager::instance()) {}

ProductsPresenter::ProductsPresenter(model::ProductsRepository& repository)
    : repository_(repository) {}

void ProductsPresenter::initialize() {
    LOG_IF(info,"ProductsPresenter initializing - loading initial products");
    loadProducts();
}

void ProductsPresenter::loadProducts() {
    LOG_IF(debug,"Loading all products (no search filter)");
    currentSearchQuery_.clear();
    auto vm = buildProductsViewModel();
    notifyProductsLoaded(vm);
}

void ProductsPresenter::searchProducts(const std::string& query) {
    LOG_IF(debug,"Search products: query=\"{}\"", query);
    currentSearchQuery_ = query;
    auto vm = buildProductsViewModel();
    notifyProductsLoaded(vm);
}

void ProductsPresenter::viewProduct(int productId) {
    LOG_IF(debug,"View product requested: id={}", productId);
    auto vm = buildProductDetailViewModel(productId);
    notifyViewProductReady(vm);
}

presenter::ProductsViewModel ProductsPresenter::buildProductsViewModel() {
    presenter::ProductsViewModel vm;

    std::vector<model::Product> dbProducts;

    // Get products from repository (excludes soft-deleted)
    if (currentSearchQuery_.empty()) {
        dbProducts = repository_.getAllProducts();
    } else {
        dbProducts = repository_.searchProducts(currentSearchQuery_);
    }

    // Transform to ViewModel
    for (const auto& dbProd : dbProducts) {
        presenter::ProductsViewModel::ProductItem item;
        item.id = dbProd.id;
        item.productCode = dbProd.productCode;
        item.name = dbProd.name;
        item.status = dbProd.status;
        item.stock = dbProd.stock;
        item.qualityRate = dbProd.qualityRate;

        vm.products.push_back(item);
    }

    LOG_IF(trace, "Built ProductsViewModel: {} products ({})",
           vm.products.size(),
           currentSearchQuery_.empty()
               ? std::string("no filter")
               : std::string("filter=\"") + currentSearchQuery_ + "\"");
    return vm;
}

presenter::ViewProductDialogViewModel ProductsPresenter::buildProductDetailViewModel(int productId) {
    presenter::ViewProductDialogViewModel vm;

    auto product = repository_.getProduct(productId);

    if (product.id == config::defaults::kInvalidProductId) {
        // Product not found
        vm.productId = "NOT FOUND";
        vm.description = "Product not found or deleted";
        vm.createdDate = "";
        vm.isVerified = false;
        return vm;
    }
    
    vm.productId = product.productCode;
    vm.description = product.name + "\n" +
                     "Status: " + product.status + "\n" +
                     "Stock: " + std::to_string(product.stock) + " units\n" +
                     "Quality: " + std::to_string(product.qualityRate) + "%";
    vm.createdDate = product.createdAt;
    vm.isVerified = (product.status == config::defaults::kStatusActive);
    
    return vm;
}

void ProductsPresenter::addProduct(const std::string& productCode, const std::string& name,
                                   const std::string& status, int stock, float qualityRate,
                                   std::function<void(bool)> callback) {
    if (!checkEditPermission(audit_, session_, logger_, "ADD",
                             std::format("code={}", productCode))) {
        callback(false);
        return;
    }
    LOG_IF(info,"Add product requested: code={}, name={}, stock={}, quality={:.1f}%",
               productCode, name, stock, qualityRate);
    auto& db = model::DatabaseManager::instance();

    // ASYNC - non-blocking database operation
    db.addProductAsync(productCode, name, status, stock, qualityRate,
                       [this, productCode, callback](bool success) {
        if (success) {
            LOG_IF(info,"Add product succeeded: code={}", productCode);
            emitAudit(audit_, session_, "ADD",
                      std::format("code={}", productCode),
                      app::auth::result::kSuccess);
            loadProducts();
        } else {
            LOG_IF(warn,"Add product failed (likely duplicate code): {}", productCode);
            emitAudit(audit_, session_, "ADD",
                      std::format("code={}", productCode),
                      app::auth::result::kFailure);
        }
        callback(success);
    });
}

void ProductsPresenter::deleteProduct(int productId, std::function<void(bool)> callback) {
    if (!checkEditPermission(audit_, session_, logger_, "DELETE",
                             std::format("id={}", productId))) {
        callback(false);
        return;
    }
    LOG_IF(info,"Delete product requested: id={}", productId);
    auto& db = model::DatabaseManager::instance();

    // ASYNC - non-blocking soft delete
    db.deleteProductAsync(productId, [this, productId, callback](bool success) {
        if (success) {
            LOG_IF(info,"Delete product succeeded: id={}", productId);
            emitAudit(audit_, session_, "DELETE",
                      std::format("id={}", productId),
                      app::auth::result::kSuccess);
            loadProducts();
        } else {
            LOG_IF(warn,"Delete product failed: id={}", productId);
            emitAudit(audit_, session_, "DELETE",
                      std::format("id={}", productId),
                      app::auth::result::kFailure);
        }
        callback(success);
    });
}

void ProductsPresenter::updateProduct(int productId, const std::string& name,
                                     const std::string& status, int stock, float qualityRate,
                                     std::function<void(bool)> callback) {
    if (!checkEditPermission(audit_, session_, logger_, "UPDATE",
                             std::format("id={}", productId))) {
        callback(false);
        return;
    }
    LOG_IF(info,"Update product requested: id={}, name={}, stock={}, quality={:.1f}%",
               productId, name, stock, qualityRate);
    auto& db = model::DatabaseManager::instance();

    // ASYNC - non-blocking update
    db.updateProductAsync(productId, name, status, stock, qualityRate,
                          [this, productId, callback](bool success) {
        if (success) {
            LOG_IF(info,"Update product succeeded: id={}", productId);
            emitAudit(audit_, session_, "UPDATE",
                      std::format("id={}", productId),
                      app::auth::result::kSuccess);
            loadProducts();
        } else {
            LOG_IF(warn,"Update product failed: id={}", productId);
            emitAudit(audit_, session_, "UPDATE",
                      std::format("id={}", productId),
                      app::auth::result::kFailure);
        }
        callback(success);
    });
}

model::DatabaseManager::Product ProductsPresenter::getProduct(int productId) {
    return repository_.getProduct(productId);
}

void ProductsPresenter::exportProducts(
        std::function<void(std::vector<model::Product>)> callback) {
    LOG_IF(info,"Export products requested");
    auto& db = model::DatabaseManager::instance();
    db.getAllProductsAsync(std::move(callback));
}

void ProductsPresenter::loadRecipe(int productId) {
    // Hookup absent (e.g. unit test, or production model not wired):
    // fail gracefully with a message rather than dereferencing null.
    if (recipes_ == nullptr || productionModel_ == nullptr) {
        LOG_IF(warn, "loadRecipe({}) called but recipe-loading hookup is absent",
               productId);
        notifyAll([](ViewObserver* obs) {
            obs->onRecipeLoaded(false, _("Recipe loading is not available."));
        });
        return;
    }

    const auto product = repository_.getProduct(productId);
    if (product.id == config::defaults::kInvalidProductId) {
        notifyAll([](ViewObserver* obs) {
            obs->onRecipeLoaded(false, _("Product not found."));
        });
        return;
    }

    const auto recipe = recipes_->getRecipeByProductCode(product.productCode);
    if (!recipe.has_value()) {
        LOG_IF(info, "No recipe defined for product {}", product.productCode);
        std::string code = product.productCode;
        const std::string msg = std::vformat(
            std::string{_("No recipe defined for {}.")},
            std::make_format_args(code));
        notifyAll([&msg](ViewObserver* obs) { obs->onRecipeLoaded(false, msg); });
        return;
    }

    productionModel_->loadProduct(product, *recipe);
    LOG_IF(info, "Loaded recipe for product {} onto the line",
           product.productCode);
    std::string name = product.name;
    const std::string msg = std::vformat(
        std::string{_("Loaded recipe for {}.")},
        std::make_format_args(name));
    notifyAll([&msg](ViewObserver* obs) { obs->onRecipeLoaded(true, msg); });
}

model::Recipe ProductsPresenter::getRecipeForEditing(int productId) {
    model::Recipe recipe;
    const auto product = repository_.getProduct(productId);
    recipe.productCode = product.productCode;

    // Start from the stored recipe (operation count) if one exists.
    std::optional<model::Recipe> stored;
    if (recipes_ != nullptr) {
        stored = recipes_->getRecipeByProductCode(product.productCode);
    }
    if (stored.has_value()) {
        recipe.totalOperations = stored->totalOperations;
    }

    // Build one target row per known quality checkpoint: keep the stored
    // value where present, default the rest. This lets the editor render
    // a complete form even for a product with no recipe yet.
    std::vector<model::QualityCheckpoint> checkpoints;
    if (productionModel_ != nullptr) {
        checkpoints = productionModel_->getQualityCheckpoints();
    }
    if (checkpoints.empty()) {
        // No model to enumerate (e.g. headless / unit test): fall back to
        // whatever the stored recipe carried.
        if (stored.has_value()) {
            recipe.checkpointTargets = stored->checkpointTargets;
        }
        return recipe;
    }
    for (const auto& cp : checkpoints) {
        model::CheckpointTarget target;
        target.checkpointName = cp.name;
        target.passRateTarget = model::kDefaultPassRateTarget;
        if (stored.has_value()) {
            for (const auto& st : stored->checkpointTargets) {
                if (st.checkpointName == cp.name) {
                    target.passRateTarget = st.passRateTarget;
                    break;
                }
            }
        }
        recipe.checkpointTargets.push_back(std::move(target));
    }
    return recipe;
}

void ProductsPresenter::saveRecipe(int productId, const model::Recipe& recipe,
                                   std::function<void(bool)> callback) {
    if (recipesWriter_ == nullptr) {
        LOG_IF(warn, "saveRecipe({}) called but recipe-writer hookup is absent",
               productId);
        callback(false);
        return;
    }
    if (!checkEditPermission(audit_, session_, logger_, "RECIPE_SAVE",
                             std::format("code={}", recipe.productCode))) {
        callback(false);
        return;
    }
    // Validate before touching storage: operation count in range, every
    // pass-rate target a sane percentage.
    if (recipe.totalOperations < kMinOperations ||
        recipe.totalOperations > kMaxOperations) {
        LOG_IF(warn, "saveRecipe: rejected totalOperations={}",
               recipe.totalOperations);
        callback(false);
        return;
    }
    for (const auto& target : recipe.checkpointTargets) {
        if (target.passRateTarget < kMinPassRate ||
            target.passRateTarget > kMaxPassRate) {
            LOG_IF(warn, "saveRecipe: rejected target {}={} (out of 0..100)",
                   target.checkpointName, target.passRateTarget);
            callback(false);
            return;
        }
    }

    // Single small operator-initiated transaction -- run it synchronously
    // through the injected writer (testable with a mock) rather than the
    // async product path.
    const bool success = recipesWriter_->upsertRecipe(recipe);
    emitAudit(audit_, session_, "RECIPE_SAVE",
              std::format("code={}", recipe.productCode),
              success ? app::auth::result::kSuccess : app::auth::result::kFailure);
    LOG_IF(info, "saveRecipe({}) -> {}", recipe.productCode,
           success ? "ok" : "failed");
    callback(success);
}

void ProductsPresenter::notifyProductsLoaded(const presenter::ProductsViewModel& vm) {
    LOG_IF(trace, "Notifying {} observer(s) of ProductsViewModel ({} products)",
           observers_.size(), vm.products.size());
    notifyAll([&vm](ViewObserver* obs) {
        obs->onProductsLoaded(vm);
    });
}

void ProductsPresenter::notifyViewProductReady(const presenter::ViewProductDialogViewModel& vm) {
    LOG_IF(trace, "Notifying {} observer(s) of ViewProductDialogViewModel ({})",
           observers_.size(), vm.productId);
    notifyAll([&vm](ViewObserver* obs) {
        obs->onViewProductReady(vm);
    });
}

}  // namespace app
