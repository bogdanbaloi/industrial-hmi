#pragma once

#include "src/auth/Role.h"
#include "src/gtk/view/pages/Page.h"
#include "src/presenter/ViewObserver.h"
#include "src/presenter/ProductsPresenter.h"
#include "src/model/DatabaseManager.h"
#include "ProductObject.h"
#include <gtkmm.h>
#include <giomm.h>
#include <memory>

class ProductsPageTest;  // forward-declare the gtest fixture

namespace app::view {

class DialogManager;

/// Products management page with GTK4 ColumnView virtual rendering
///
/// Uses ColumnView + Gio::ListStore + SignalListItemFactory for
/// efficient rendering - only visible rows have allocated widgets.
class ProductsPage : public Page, public ViewObserver {
    friend class ::ProductsPageTest;  // test access to private handlers
public:
    explicit ProductsPage(DialogManager& dialogManager);
    ~ProductsPage() override;

    void initialize(std::shared_ptr<ProductsPresenter> presenter);

    /// Gate CRUD controls by role. Operator gets view-only -- the Add
    /// button + per-row Edit / Delete actions become insensitive
    /// with a tooltip pointing at the missing role. Maintenance and
    /// Admin get the full surface.
    void applyRole(app::auth::Role role);

    // Page overrides
    [[nodiscard]] Glib::ustring pageTitle() const override;

    // ViewObserver interface
    void onProductsLoaded(const presenter::ProductsViewModel& vm) override;
    void onViewProductReady(const presenter::ViewProductDialogViewModel& vm) override;
    void onRecipeLoaded(bool success, const std::string& message) override;

private:
    // UI construction -- loads layout from assets/ui/products-page.ui and
    // injects the ColumnView (programmatic: factories + list model) into
    // the ScrolledWindow container defined in XML.
    void buildUI();

    // Event handlers
    void onSearchChanged();
    void onRefreshClicked();
    void onProductActivated(guint position);
    void onAddProductClicked();
    void onViewProductClicked();
    void onEditProductClicked();
    void onDeleteProductClicked();
    void onLoadRecipeClicked();
    void onEditRecipeClicked();
    void onExportCsvClicked();
    void exportToCsv(const std::string& path,
                     const std::vector<model::Product>& products);

    // Helper methods
    void updateProductsList(const presenter::ProductsViewModel& vm);
    void showProductDetail(const presenter::ViewProductDialogViewModel& vm);
    void showAddProductDialog();
    void showEditProductDialog(const model::DatabaseManager::Product& product);
    void showEditRecipeDialog(const model::DatabaseManager::Product& product);
    void showDeleteConfirmDialog(int productId, const std::string& productName);
    int getSelectedProductId();

    // Widgets
    Gtk::SearchEntry* searchEntry_{nullptr};
    Gtk::Button* refreshButton_{nullptr};
    Gtk::Button* addButton_{nullptr};
    Gtk::Button* exportButton_{nullptr};
    Gtk::ScrolledWindow* scrolledWindow_{nullptr};
    Gtk::ColumnView* columnView_{nullptr};

    // ColumnView model (virtual rendering)
    Glib::RefPtr<Gio::ListStore<ProductObject>> listStore_;
    Glib::RefPtr<Gtk::SingleSelection> selectionModel_;

    std::shared_ptr<ProductsPresenter> presenter_;

    /// Cached active role, applied via applyRole(). Default Admin so
    /// the no-auth dev path stays unchanged (every action allowed
    /// when the page is built outside a session). MainWindow calls
    /// applyRole() with the actual role after constructing the page.
    app::auth::Role role_{app::auth::Role::Admin};

    // CSS
    Glib::RefPtr<Gtk::CssProvider> cssProvider_;
    void applyStyles();
};

}  // namespace app::view
