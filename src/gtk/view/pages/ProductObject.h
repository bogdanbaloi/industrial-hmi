#pragma once

#include <glibmm.h>
#include <string>

namespace app::view {

/// GObject wrapper for product data in ColumnView
///
/// Gio::ListStore requires items that derive from Glib::Object.
/// This lightweight wrapper stores product fields and provides
/// a static factory for creating RefPtr instances.
class ProductObject : public Glib::Object {
public:
    static Glib::RefPtr<ProductObject> create(
            int id,
            const std::string& productCode,
            const std::string& name,
            const std::string& status,
            int stock,
            float qualityRate) {
        return Glib::make_refptr_for_instance<ProductObject>(
            new ProductObject(id, productCode, name, status, stock, qualityRate));
    }

    int getId() const { return id_; }
    const std::string& getProductCode() const { return productCode_; }
    const std::string& getName() const { return name_; }
    const std::string& getStatus() const { return status_; }
    int getStock() const { return stock_; }
    float getQualityRate() const { return qualityRate_; }

private:
    ProductObject(int id,
                  const std::string& productCode,
                  const std::string& name,
                  const std::string& status,
                  int stock,
                  float qualityRate)
        : Glib::ObjectBase("ProductObject")
        , id_(id)
        , productCode_(productCode)
        , name_(name)
        , status_(status)
        , stock_(stock)
        , qualityRate_(qualityRate) {}

    int id_;
    std::string productCode_;
    std::string name_;
    std::string status_;
    int stock_;
    float qualityRate_;
};

}  // namespace app::view
