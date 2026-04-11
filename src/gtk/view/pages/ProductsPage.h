#pragma once

#include "src/presenter/ViewObserver.h"
#include "src/presenter/ProductsPresenter.h"
#include "src/model/DatabaseManager.h"
#include <gtkmm.h>
#include <memory>

namespace app::view {

// Forward declaration
class DialogManager;

/// Products management page - demonstrates database integration
///
/// Shows how to build a data-driven View:
/// - List view with database records
/// - Search/filter functionality
/// - Detail view dialog
/// - CRUD operations with DialogManager injection
/// 
/// @design Dependency Injection - DialogManager injected via constructor
class ProductsPage : public Gtk::Box, public ViewObserver {
public:
    /// Constructor with dependency injection
    /// @param dialogManager Reference to DialogManager (injected)
    explicit ProductsPage(DialogManager& dialogManager);
    ~ProductsPage() override;

    void initialize(std::shared_ptr<ProductsPresenter> presenter);

    // ViewObserver interface
    void onProductsLoaded(const presenter::ProductsViewModel& vm) override;
    void onViewProductReady(const presenter::ViewProductDialogViewModel& vm) override;

private:
    // UI construction
    void buildUI();
    void buildSearchBar();
    void buildProductsList();
    void buildToolbar();
    
    // Event handlers
    void onSearchChanged();
    void onRefreshClicked();
    void onProductSelected(const Gtk::TreeModel::Path& path);
    void onAddProductClicked();
    void onViewProductClicked();
    void onEditProductClicked();
    void onDeleteProductClicked();
    
    // Helper methods
    void updateProductsList(const presenter::ProductsViewModel& vm);
    void showProductDetail(const presenter::ViewProductDialogViewModel& vm);
    void showAddProductDialog();
    void showEditProductDialog(const model::DatabaseManager::Product& product);
    void showDeleteConfirmDialog(int productId, const std::string& productName);
    int getSelectedProductId();
    
    // Widgets
    Gtk::SearchEntry* searchEntry_{nullptr};
    Gtk::Button* refreshButton_{nullptr};
    Gtk::Button* addButton_{nullptr};
    Gtk::ScrolledWindow* scrolledWindow_{nullptr};
    Gtk::TreeView* treeView_{nullptr};
    
    // TreeView model
    struct ProductColumns : public Gtk::TreeModel::ColumnRecord {
        Gtk::TreeModelColumn<int> id;
        Gtk::TreeModelColumn<Glib::ustring> productCode;
        Gtk::TreeModelColumn<Glib::ustring> name;
        Gtk::TreeModelColumn<Glib::ustring> status;
        Gtk::TreeModelColumn<int> stock;
        Gtk::TreeModelColumn<float> qualityRate;
    };
    ProductColumns columns_;
    Glib::RefPtr<Gtk::ListStore> listStore_;
    
    // Injected dependencies
    DialogManager& dialogManager_;
    
    // Presenter reference
    std::shared_ptr<ProductsPresenter> presenter_;
    
    // CSS
    Glib::RefPtr<Gtk::CssProvider> cssProvider_;
    void applyStyles();
};

}  // namespace app::view
