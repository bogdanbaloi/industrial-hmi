#include "ProductsPresenter.h"
#include "src/model/DatabaseManager.h"
#include "src/config/config_defaults.h"

// See DashboardPresenter.cpp — nullable logger_ inherited from BasePresenter.
#define LOG_IF(LEVEL, ...) do { if (logger_) logger_->LEVEL(__VA_ARGS__); } while (0)

namespace app {

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
    LOG_IF(info,"Add product requested: code={}, name={}, stock={}, quality={:.1f}%",
               productCode, name, stock, qualityRate);
    auto& db = model::DatabaseManager::instance();

    // ASYNC - non-blocking database operation
    db.addProductAsync(productCode, name, status, stock, qualityRate,
                       [this, productCode, callback](bool success) {
        if (success) {
            LOG_IF(info,"Add product succeeded: code={}", productCode);
            loadProducts();
        } else {
            LOG_IF(warn,"Add product failed (likely duplicate code): {}", productCode);
        }
        callback(success);
    });
}

void ProductsPresenter::deleteProduct(int productId, std::function<void(bool)> callback) {
    LOG_IF(info,"Delete product requested: id={}", productId);
    auto& db = model::DatabaseManager::instance();

    // ASYNC - non-blocking soft delete
    db.deleteProductAsync(productId, [this, productId, callback](bool success) {
        if (success) {
            LOG_IF(info,"Delete product succeeded: id={}", productId);
            loadProducts();
        } else {
            LOG_IF(warn,"Delete product failed: id={}", productId);
        }
        callback(success);
    });
}

void ProductsPresenter::updateProduct(int productId, const std::string& name,
                                     const std::string& status, int stock, float qualityRate,
                                     std::function<void(bool)> callback) {
    LOG_IF(info,"Update product requested: id={}, name={}, stock={}, quality={:.1f}%",
               productId, name, stock, qualityRate);
    auto& db = model::DatabaseManager::instance();

    // ASYNC - non-blocking update
    db.updateProductAsync(productId, name, status, stock, qualityRate,
                          [this, productId, callback](bool success) {
        if (success) {
            LOG_IF(info,"Update product succeeded: id={}", productId);
            loadProducts();
        } else {
            LOG_IF(warn,"Update product failed: id={}", productId);
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
