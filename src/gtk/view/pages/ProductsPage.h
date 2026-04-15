#pragma once

#include "src/presenter/ViewObserver.h"
#include "src/presenter/ProductsPresenter.h"
#include "src/model/DatabaseManager.h"
#include "ProductObject.h"
#include <gtkmm.h>
#include <giomm.h>
#include <memory>

namespace app::view {

class DialogManager;

/// Products management page with GTK4 ColumnView virtual rendering
///
/// Uses ColumnView + Gio::ListStore + SignalListItemFactory for
/// efficient rendering - only visible rows have allocated widgets.
class ProductsPage : public Gtk::Box, public ViewObserver {
public:
    explicit ProductsPage(DialogManager& dialogManager);
    ~ProductsPage() override;

    void initialize(std::shared_ptr<ProductsPresenter> presenter);

    // ViewObserver interface
    void onProductsLoaded(const presenter::ProductsViewModel& vm) override;
    void onViewProductReady(const presenter::ViewProductDialogViewModel& vm) override;

private:
    void buildUI();
    void buildSearchBar();
    void buildProductsList();
    void buildToolbar();

    // Event handlers
    void onSearchChanged();
    void onRefreshClicked();
    void onProductActivated(guint position);
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
    Gtk::ColumnView* columnView_{nullptr};

    // ColumnView model (virtual rendering)
    Glib::RefPtr<Gio::ListStore<ProductObject>> listStore_;
    Glib::RefPtr<Gtk::SingleSelection> selectionModel_;

    // Injected dependencies
    DialogManager& dialogManager_;
    std::shared_ptr<ProductsPresenter> presenter_;

    // CSS
    Glib::RefPtr<Gtk::CssProvider> cssProvider_;
    void applyStyles();
};

}  // namespace app::view
