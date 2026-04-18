#include "ProductsPresenter.h"
#include "src/model/DatabaseManager.h"
#include "src/config/config_defaults.h"
#include "src/core/Application.h"

namespace app {

namespace {
inline app::core::Logger& log() {
    return app::core::Application::instance().logger();
}
}  // namespace

ProductsPresenter::ProductsPresenter()
    : ProductsPresenter(model::DatabaseManager::instance()) {}

ProductsPresenter::ProductsPresenter(model::ProductsRepository& repository)
    : repository_(repository) {}

void ProductsPresenter::initialize() {
    log().info("ProductsPresenter initializing - loading initial products");
    loadProducts();
}

void ProductsPresenter::loadProducts() {
    log().debug("Loading all products (no search filter)");
    currentSearchQuery_.clear();
    auto vm = buildProductsViewModel();
    notifyProductsLoaded(vm);
}

void ProductsPresenter::searchProducts(const std::string& query) {
    log().debug("Search products: query=\"{}\"", query);
    currentSearchQuery_ = query;
    auto vm = buildProductsViewModel();
    notifyProductsLoaded(vm);
}

void ProductsPresenter::viewProduct(int productId) {
    log().debug("View product requested: id={}", productId);
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

    log().trace("Built ProductsViewModel with {} rows (query=\"{}\")",
                vm.products.size(), currentSearchQuery_);
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
    log().info("Add product requested: code={}, name={}, stock={}, quality={:.1f}%",
               productCode, name, stock, qualityRate);
    auto& db = model::DatabaseManager::instance();

    // ASYNC - non-blocking database operation
    db.addProductAsync(productCode, name, status, stock, qualityRate,
                       [this, productCode, callback](bool success) {
        if (success) {
            log().info("Add product succeeded: code={}", productCode);
            loadProducts();
        } else {
            log().warn("Add product failed (likely duplicate code): {}", productCode);
        }
        callback(success);
    });
}

void ProductsPresenter::deleteProduct(int productId, std::function<void(bool)> callback) {
    log().info("Delete product requested: id={}", productId);
    auto& db = model::DatabaseManager::instance();

    // ASYNC - non-blocking soft delete
    db.deleteProductAsync(productId, [this, productId, callback](bool success) {
        if (success) {
            log().info("Delete product succeeded: id={}", productId);
            loadProducts();
        } else {
            log().warn("Delete product failed: id={}", productId);
        }
        callback(success);
    });
}

void ProductsPresenter::updateProduct(int productId, const std::string& name,
                                     const std::string& status, int stock, float qualityRate,
                                     std::function<void(bool)> callback) {
    log().info("Update product requested: id={}, name={}, stock={}, quality={:.1f}%",
               productId, name, stock, qualityRate);
    auto& db = model::DatabaseManager::instance();

    // ASYNC - non-blocking update
    db.updateProductAsync(productId, name, status, stock, qualityRate,
                          [this, productId, callback](bool success) {
        if (success) {
            log().info("Update product succeeded: id={}", productId);
            loadProducts();
        } else {
            log().warn("Update product failed: id={}", productId);
        }
        callback(success);
    });
}

model::DatabaseManager::Product ProductsPresenter::getProduct(int productId) {
    return repository_.getProduct(productId);
}

void ProductsPresenter::exportProducts(
        std::function<void(std::vector<model::Product>)> callback) {
    log().info("Export products requested");
    auto& db = model::DatabaseManager::instance();
    db.getAllProductsAsync(std::move(callback));
}

void ProductsPresenter::notifyProductsLoaded(const presenter::ProductsViewModel& vm) {
    log().trace("Notifying {} observers of ProductsViewModel ({} products)",
                /*cnt*/ 0, vm.products.size());
    notifyAll([&vm](ViewObserver* obs) {
        obs->onProductsLoaded(vm);
    });
}

void ProductsPresenter::notifyViewProductReady(const presenter::ViewProductDialogViewModel& vm) {
    log().trace("Notifying observers of ViewProductDialogViewModel ({})", vm.productId);
    notifyAll([&vm](ViewObserver* obs) {
        obs->onViewProductReady(vm);
    });
}

}  // namespace app
