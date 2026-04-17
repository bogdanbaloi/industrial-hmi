#include "ProductsPresenter.h"
#include "src/model/DatabaseManager.h"
#include "src/config/config_defaults.h"

namespace app {

ProductsPresenter::ProductsPresenter()
    : ProductsPresenter(model::DatabaseManager::instance()) {}

ProductsPresenter::ProductsPresenter(model::ProductsRepository& repository)
    : repository_(repository) {}

void ProductsPresenter::initialize() {
    // Load initial products list
    loadProducts();
}

void ProductsPresenter::loadProducts() {
    currentSearchQuery_.clear();
    auto vm = buildProductsViewModel();
    notifyProductsLoaded(vm);
}

void ProductsPresenter::searchProducts(const std::string& query) {
    currentSearchQuery_ = query;
    auto vm = buildProductsViewModel();
    notifyProductsLoaded(vm);
}

void ProductsPresenter::viewProduct(int productId) {
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
    auto& db = model::DatabaseManager::instance();
    
    // ASYNC - non-blocking database operation
    db.addProductAsync(productCode, name, status, stock, qualityRate, [this, callback](bool success) {
        // This callback runs on GTK main thread (thanks to Glib::signal_idle)
        if (success) {
            // Reload products list to show new product
            loadProducts();
        }
        
        // Notify caller of result
        callback(success);
    });
}

void ProductsPresenter::deleteProduct(int productId, std::function<void(bool)> callback) {
    auto& db = model::DatabaseManager::instance();
    
    // ASYNC - non-blocking soft delete
    db.deleteProductAsync(productId, [this, callback](bool success) {
        // This callback runs on GTK main thread
        if (success) {
            // Reload products list (deleted product won't appear)
            loadProducts();
        }
        
        // Notify caller of result
        callback(success);
    });
}

void ProductsPresenter::updateProduct(int productId, const std::string& name,
                                     const std::string& status, int stock, float qualityRate,
                                     std::function<void(bool)> callback) {
    auto& db = model::DatabaseManager::instance();
    
    // ASYNC - non-blocking update
    db.updateProductAsync(productId, name, status, stock, qualityRate, [this, callback](bool success) {
        // This callback runs on GTK main thread
        if (success) {
            // Reload products list to show updated data
            loadProducts();
        }
        
        // Notify caller of result
        callback(success);
    });
}

model::DatabaseManager::Product ProductsPresenter::getProduct(int productId) {
    return repository_.getProduct(productId);
}

void ProductsPresenter::exportProducts(
        std::function<void(std::vector<model::Product>)> callback) {
    auto& db = model::DatabaseManager::instance();
    db.getAllProductsAsync(std::move(callback));
}

void ProductsPresenter::notifyProductsLoaded(const presenter::ProductsViewModel& vm) {
    notifyAll([&vm](ViewObserver* obs) {
        obs->onProductsLoaded(vm);
    });
}

void ProductsPresenter::notifyViewProductReady(const presenter::ViewProductDialogViewModel& vm) {
    notifyAll([&vm](ViewObserver* obs) {
        obs->onViewProductReady(vm);
    });
}

}  // namespace app
