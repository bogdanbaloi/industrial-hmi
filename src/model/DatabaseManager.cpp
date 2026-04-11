#include "src/model/DatabaseManager.h"
#include "src/model/ModelContext.h"
#include <glibmm/main.h>

namespace app::model {

void DatabaseManager::addProductAsync(
    const std::string& productCode,
    const std::string& name,
    const std::string& status,
    int stock,
    float qualityRate,
    std::function<void(bool)> callback)
{
    // Post to I/O thread (non-blocking)
    ModelContext::instance().post([this, productCode, name, status, stock, qualityRate, callback]() {
        // Execute on I/O thread (this blocks, but NOT the UI thread!)
        bool success = addProduct(productCode, name, status, stock, qualityRate);
        
        // Marshal result back to GTK main thread
        Glib::signal_idle().connect_once([callback, success]() {
            callback(success);
        });
    });
}

void DatabaseManager::updateProductAsync(
    int id,
    const std::string& name,
    const std::string& status,
    int stock,
    float qualityRate,
    std::function<void(bool)> callback)
{
    // Post to I/O thread (non-blocking)
    ModelContext::instance().post([this, id, name, status, stock, qualityRate, callback]() {
        // Execute on I/O thread
        bool success = updateProduct(id, name, status, stock, qualityRate);
        
        // Marshal result back to GTK main thread
        Glib::signal_idle().connect_once([callback, success]() {
            callback(success);
        });
    });
}

void DatabaseManager::deleteProductAsync(
    int id,
    std::function<void(bool)> callback)
{
    // Post to I/O thread (non-blocking)
    ModelContext::instance().post([this, id, callback]() {
        // Execute on I/O thread
        bool success = deleteProduct(id);
        
        // Marshal result back to GTK main thread
        Glib::signal_idle().connect_once([callback, success]() {
            callback(success);
        });
    });
}

void DatabaseManager::getAllProductsAsync(
    std::function<void(std::vector<Product>)> callback)
{
    // Post to I/O thread (non-blocking)
    ModelContext::instance().post([this, callback]() {
        // Execute on I/O thread
        std::vector<Product> products = getAllProducts();
        
        // Marshal result back to GTK main thread
        Glib::signal_idle().connect_once([callback, products]() {
            callback(products);
        });
    });
}

} // namespace app::model
