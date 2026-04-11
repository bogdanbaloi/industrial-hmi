#pragma once

#include "src/presenter/ViewObserver.h"
#include "src/presenter/ProductsPresenter.h"
#include <gtkmm.h>
#include <memory>

namespace app::view {

/// Products management page - demonstrates database integration
///
/// Shows how to build a data-driven View:
/// - List view with database records
/// - Search/filter functionality
/// - Detail view dialog
/// - CRUD operations (Read in this demo)
class ProductsPage : public Gtk::Box, public ViewObserver {
public:
    ProductsPage();
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
    
    // Helper methods
    void updateProductsList(const presenter::ProductsViewModel& vm);
    void showProductDetail(const presenter::ViewProductDialogViewModel& vm);
    
    // Widgets
    Gtk::SearchEntry* searchEntry_{nullptr};
    Gtk::Button* refreshButton_{nullptr};
    Gtk::ScrolledWindow* scrolledWindow_{nullptr};
    Gtk::TreeView* treeView_{nullptr};
    
    // TreeView model
    struct ProductColumns : public Gtk::TreeModel::ColumnRecord {
        Gtk::TreeModelColumn<int> id;
        Gtk::TreeModelColumn<Glib::ustring> productId;
        Gtk::TreeModelColumn<Glib::ustring> description;
        Gtk::TreeModelColumn<Glib::ustring> status;
    };
    ProductColumns columns_;
    Glib::RefPtr<Gtk::ListStore> listStore_;
    
    // Presenter reference
    std::shared_ptr<ProductsPresenter> presenter_;
    
    // CSS
    Glib::RefPtr<Gtk::CssProvider> cssProvider_;
    void applyStyles();
};

}  // namespace app::view
