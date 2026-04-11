#include "ProductsPresenter.h"
#include "src/model/DatabaseManager.h"

namespace app {

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
    
    auto& db = model::DatabaseManager::instance();
    std::vector<model::DatabaseManager::Product> dbProducts;
    
    // Get products from database
    if (currentSearchQuery_.empty()) {
        dbProducts = db.getAllProducts();
    } else {
        dbProducts = db.searchProducts(currentSearchQuery_);
    }
    
    // Transform to ViewModel
    for (const auto& dbProd : dbProducts) {
        presenter::ProductsViewModel::ProductItem item;
        item.id = dbProd.id;
        item.productId = dbProd.productId;
        item.description = dbProd.name;
        item.isVerified = dbProd.isActive;
        
        vm.products.push_back(item);
    }
    
    return vm;
}

presenter::ViewProductDialogViewModel ProductsPresenter::buildProductDetailViewModel(int productId) {
    presenter::ViewProductDialogViewModel vm;
    
    auto& db = model::DatabaseManager::instance();
    auto product = db.getProduct(productId);
    
    vm.productId = product.productId;
    vm.description = product.name + "\n\n" + product.description;
    vm.createdDate = product.createdDate;
    vm.isVerified = product.isActive;
    
    return vm;
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
