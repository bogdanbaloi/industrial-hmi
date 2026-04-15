#include "ProductsPage.h"
#include "src/gtk/view/DialogManager.h"
#include "src/gtk/view/ThemeManager.h"
#include "src/config/config_defaults.h"

namespace app::view {

ProductsPage::ProductsPage(DialogManager& dialogManager)
    : Gtk::Box(Gtk::Orientation::VERTICAL)
    , dialogManager_(dialogManager) {
    buildUI();
    applyStyles();
}

ProductsPage::~ProductsPage() {
    if (presenter_) {
        presenter_->removeObserver(this);
    }
}

void ProductsPage::initialize(std::shared_ptr<ProductsPresenter> presenter) {
    presenter_ = presenter;
    presenter_->addObserver(this);
    presenter_->initialize();
}

// ViewObserver implementation
void ProductsPage::onProductsLoaded(const presenter::ProductsViewModel& vm) {
    // Marshal to GTK thread
    Glib::signal_idle().connect_once([this, vm]() {
        updateProductsList(vm);
    });
}

void ProductsPage::onViewProductReady(const presenter::ViewProductDialogViewModel& vm) {
    // Marshal to GTK thread
    Glib::signal_idle().connect_once([this, vm]() {
        showProductDetail(vm);
    });
}

// UI Construction
void ProductsPage::buildUI() {
    set_spacing(20);
    set_margin_start(60);
    set_margin_end(60);
    set_margin_top(40);
    set_margin_bottom(40);
    
    buildToolbar();
    buildSearchBar();
    buildProductsList();
}

void ProductsPage::buildToolbar() {
    auto* toolbar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 15);
    toolbar->set_margin_bottom(20);
    
    // Title
    auto* title = Gtk::make_managed<Gtk::Label>("Products Database");
    title->add_css_class("page-title");
    title->set_hexpand(true);
    title->set_xalign(0.0);
    toolbar->append(*title);
    
    // Add Product button
    addButton_ = Gtk::make_managed<Gtk::Button>("+ Add New Product");
    addButton_->add_css_class("toolbar-button");
    addButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onAddProductClicked)
    );
    toolbar->append(*addButton_);
    
    // Refresh button
    refreshButton_ = Gtk::make_managed<Gtk::Button>("Refresh");
    refreshButton_->add_css_class("toolbar-button");
    refreshButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onRefreshClicked)
    );
    toolbar->append(*refreshButton_);
    
    append(*toolbar);
}

void ProductsPage::buildSearchBar() {
    auto* searchBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    searchBox->set_margin_bottom(20);
    
    auto* searchLabel = Gtk::make_managed<Gtk::Label>("Search:");
    searchLabel->add_css_class("search-label");
    searchBox->append(*searchLabel);
    
    searchEntry_ = Gtk::make_managed<Gtk::SearchEntry>();
    searchEntry_->set_placeholder_text("Search by Product ID or Name...");
    searchEntry_->set_hexpand(true);
    searchEntry_->signal_search_changed().connect(
        sigc::mem_fun(*this, &ProductsPage::onSearchChanged)
    );
    searchBox->append(*searchEntry_);
    
    append(*searchBox);
}

void ProductsPage::buildProductsList() {
    // GTK4 ColumnView with virtual rendering
    // Only visible rows have allocated widgets (widget recycling)
    listStore_ = Gio::ListStore<ProductObject>::create();
    selectionModel_ = Gtk::SingleSelection::create(listStore_);

    columnView_ = Gtk::make_managed<Gtk::ColumnView>(selectionModel_);
    columnView_->set_hexpand(true);
    columnView_->set_vexpand(true);
    columnView_->set_show_column_separators(true);
    columnView_->set_show_row_separators(true);

    // Helper to create a text column with SignalListItemFactory
    auto makeColumn = [](const Glib::ustring& title, int fixedWidth, bool expand,
                         std::function<Glib::ustring(const Glib::RefPtr<ProductObject>&)> getter) {
        auto factory = Gtk::SignalListItemFactory::create();

        factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto* label = Gtk::make_managed<Gtk::Label>();
            label->set_xalign(0.0);
            label->set_margin_start(8);
            label->set_margin_end(8);
            item->set_child(*label);
        });

        factory->signal_bind().connect([getter](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto obj = std::dynamic_pointer_cast<ProductObject>(item->get_item());
            auto* label = dynamic_cast<Gtk::Label*>(item->get_child());
            if (obj && label) {
                label->set_text(getter(obj));
            }
        });

        auto column = Gtk::ColumnViewColumn::create(title, factory);
        if (fixedWidth > 0) column->set_fixed_width(fixedWidth);
        if (expand) column->set_expand(true);
        column->set_resizable(true);
        return column;
    };

    // Add columns
    columnView_->append_column(makeColumn("Product Code", 120, false,
        [](const Glib::RefPtr<ProductObject>& p) { return p->getProductCode(); }));

    columnView_->append_column(makeColumn("Name", 0, true,
        [](const Glib::RefPtr<ProductObject>& p) { return p->getName(); }));

    columnView_->append_column(makeColumn("Status", 120, false,
        [](const Glib::RefPtr<ProductObject>& p) { return p->getStatus(); }));

    columnView_->append_column(makeColumn("Stock", 80, false,
        [](const Glib::RefPtr<ProductObject>& p) { return std::to_string(p->getStock()); }));

    columnView_->append_column(makeColumn("Quality %", 100, false,
        [](const Glib::RefPtr<ProductObject>& p) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.1f%%", p->getQualityRate());
            return Glib::ustring(buf);
        }));

    // Row activation (double-click)
    columnView_->signal_activate().connect(
        sigc::mem_fun(*this, &ProductsPage::onProductActivated));

    // ScrolledWindow
    scrolledWindow_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    scrolledWindow_->set_child(*columnView_);
    scrolledWindow_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scrolledWindow_->set_vexpand(true);

    // Action buttons
    auto* actionBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    actionBox->set_margin_top(10);

    auto* viewButton = Gtk::make_managed<Gtk::Button>("View Details");
    viewButton->add_css_class("toolbar-button");
    viewButton->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onViewProductClicked));
    actionBox->append(*viewButton);

    auto* editButton = Gtk::make_managed<Gtk::Button>("Edit");
    editButton->add_css_class("toolbar-button");
    editButton->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onEditProductClicked));
    actionBox->append(*editButton);

    auto* deleteButton = Gtk::make_managed<Gtk::Button>("Delete");
    deleteButton->add_css_class("toolbar-button");
    deleteButton->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onDeleteProductClicked));
    actionBox->append(*deleteButton);

    append(*scrolledWindow_);
    append(*actionBox);
}

// Event Handlers
void ProductsPage::onSearchChanged() {
    if (presenter_) {
        std::string query = searchEntry_->get_text();
        presenter_->searchProducts(query);
    }
}

void ProductsPage::onRefreshClicked() {
    if (presenter_) {
        searchEntry_->set_text("");
        presenter_->loadProducts();
    }
}

void ProductsPage::onProductActivated(guint position) {
    auto item = listStore_->get_item(position);
    if (item && presenter_) {
        presenter_->viewProduct(item->getId());
    }
}

void ProductsPage::updateProductsList(const presenter::ProductsViewModel& vm) {
    listStore_->remove_all();

    for (const auto& product : vm.products) {
        listStore_->append(ProductObject::create(
            product.id, product.productCode, product.name,
            product.status, product.stock, product.qualityRate));
    }
}

void ProductsPage::showProductDetail(const presenter::ViewProductDialogViewModel& vm) {
    auto* dialog = new Gtk::MessageDialog(*dynamic_cast<Gtk::Window*>(get_root()),
                                          "Product Details",
                                          false,
                                          Gtk::MessageType::INFO,
                                          Gtk::ButtonsType::OK);

    std::string message = "Product ID: " + vm.productId + "\n\n" +
                         vm.description + "\n\n" +
                         "Created: " + vm.createdDate + "\n" +
                         "Status: " + std::string(vm.isVerified ? config::defaults::kStatusActive : config::defaults::kStatusInactive);

    dialog->set_secondary_text(message);
    dialog->set_modal(true);
    app::view::ThemeManager::instance().applyToDialog(dialog);

    dialog->signal_response().connect([dialog](int) {
        delete dialog;
    });
    dialog->present();
}

void ProductsPage::onAddProductClicked() {
    showAddProductDialog();
}

void ProductsPage::onViewProductClicked() {
    int productId = getSelectedProductId();
    if (productId != config::defaults::kInvalidProductId && presenter_) {
        presenter_->viewProduct(productId);
    }
}

void ProductsPage::onDeleteProductClicked() {
    int productId = getSelectedProductId();
    if (productId != config::defaults::kInvalidProductId && presenter_) {
        auto product = presenter_->getProduct(productId);
        if (product.id != config::defaults::kInvalidProductId) {
            showDeleteConfirmDialog(productId, product.name);
        }
    }
}

void ProductsPage::onEditProductClicked() {
    int productId = getSelectedProductId();
    if (productId != config::defaults::kInvalidProductId && presenter_) {
        auto product = presenter_->getProduct(productId);
        if (product.id != config::defaults::kInvalidProductId) {
            showEditProductDialog(product);
        }
    }
}

int ProductsPage::getSelectedProductId() {
    auto pos = selectionModel_->get_selected();
    if (pos != GTK_INVALID_LIST_POSITION) {
        auto item = listStore_->get_item(pos);
        if (item) return item->getId();
    }
    return config::defaults::kInvalidProductId;
}

void ProductsPage::showAddProductDialog() {
    auto* dialog = new Gtk::Dialog("Add New Product", *dynamic_cast<Gtk::Window*>(get_root()));
    dialog->set_default_size(400, 350);
    dialog->set_modal(true);
    app::view::ThemeManager::instance().applyToDialog(dialog);
    
    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(12);
    grid->set_column_spacing(12);
    grid->set_margin(20);
    
    // Product Code
    auto* codeLabel = Gtk::make_managed<Gtk::Label>("Product Code:");
    codeLabel->set_xalign(0);
    auto* codeEntry = Gtk::make_managed<Gtk::Entry>();
    codeEntry->set_placeholder_text("PROD-007");
    codeEntry->set_hexpand(true);
    grid->attach(*codeLabel, 0, 0);
    grid->attach(*codeEntry, 1, 0);
    
    // Name
    auto* nameLabel = Gtk::make_managed<Gtk::Label>("Name:");
    nameLabel->set_xalign(0);
    auto* nameEntry = Gtk::make_managed<Gtk::Entry>();
    nameEntry->set_placeholder_text("Product G");
    nameEntry->set_hexpand(true);
    grid->attach(*nameLabel, 0, 1);
    grid->attach(*nameEntry, 1, 1);
    
    // Status dropdown
    auto* statusLabel = Gtk::make_managed<Gtk::Label>("Status:");
    statusLabel->set_xalign(0);
    auto* statusCombo = Gtk::make_managed<Gtk::ComboBoxText>();
    statusCombo->append(config::defaults::kStatusActive);
    statusCombo->append(config::defaults::kStatusInactive);
    statusCombo->append(config::defaults::kStatusLowStock);
    statusCombo->set_active(0);
    grid->attach(*statusLabel, 0, 2);
    grid->attach(*statusCombo, 1, 2);
    
    // Stock
    auto* stockLabel = Gtk::make_managed<Gtk::Label>("Stock (units):");
    stockLabel->set_xalign(0);
    auto* stockSpin = Gtk::make_managed<Gtk::SpinButton>();
    stockSpin->set_range(0, 10000);
    stockSpin->set_increments(10, 100);
    stockSpin->set_value(100);
    stockSpin->set_hexpand(true);
    grid->attach(*stockLabel, 0, 3);
    grid->attach(*stockSpin, 1, 3);
    
    // Quality Rate
    auto* qualityLabel = Gtk::make_managed<Gtk::Label>("Quality (%):");
    qualityLabel->set_xalign(0);
    auto* qualitySpin = Gtk::make_managed<Gtk::SpinButton>();
    qualitySpin->set_range(0.0, 100.0);
    qualitySpin->set_digits(1);
    qualitySpin->set_increments(0.1, 1.0);
    qualitySpin->set_value(95.0);
    qualitySpin->set_hexpand(true);
    grid->attach(*qualityLabel, 0, 4);
    grid->attach(*qualitySpin, 1, 4);
    
    dialog->get_content_area()->append(*grid);
    
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Save", Gtk::ResponseType::OK);
    
    dialog->signal_response().connect([this, dialog, codeEntry, nameEntry, 
                                       statusCombo, stockSpin, qualitySpin]
                                      (int response) {
        if (response == Gtk::ResponseType::OK) {
            std::string code = codeEntry->get_text();
            std::string name = nameEntry->get_text();
            std::string status = statusCombo->get_active_text();
            int stock = stockSpin->get_value_as_int();
            float quality = qualitySpin->get_value();
            
            if (!code.empty() && !name.empty() && presenter_) {
                // ASYNC - non-blocking database operation
                presenter_->addProduct(code, name, status, stock, quality, [this](bool success) {
                    // Callback runs on GTK main thread
                    if (!success) {
                        auto* parent = dynamic_cast<Gtk::Window*>(get_root());
                        dialogManager_.showError("Error", 
                                   "Failed to add product. Product code may already exist.",
                                   parent);
                    }
                });
            }
        }
        delete dialog;
    });

    dialog->present();
}

void ProductsPage::showDeleteConfirmDialog(int productId, const std::string& productName) {
    auto* parent = dynamic_cast<Gtk::Window*>(get_root());
    
    dialogManager_.showConfirmAsync(
        "Confirm Delete",
        "Delete \"" + productName + "\"?\n\n" +
        "This will mark the product as inactive (soft delete).\n" +
        "The product will no longer appear in the list.",
        [this, productId](bool confirmed) {
            if (confirmed && presenter_) {
                // ASYNC - non-blocking soft delete
                presenter_->deleteProduct(productId, [this](bool success) {
                    // Callback runs on GTK main thread
                    if (!success) {
                        auto* parent = dynamic_cast<Gtk::Window*>(get_root());
                        dialogManager_.showError("Error", 
                                   "Failed to delete product.",
                                   parent);
                    }
                });
            }
        },
        parent
    );
}

void ProductsPage::showEditProductDialog(const model::DatabaseManager::Product& product) {
    auto* dialog = new Gtk::Dialog("Edit Product", *dynamic_cast<Gtk::Window*>(get_root()));
    dialog->set_default_size(400, 350);
    dialog->set_modal(true);
    app::view::ThemeManager::instance().applyToDialog(dialog);
    
    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(12);
    grid->set_column_spacing(12);
    grid->set_margin(20);
    
    // Product Code (read-only display)
    auto* codeLabel = Gtk::make_managed<Gtk::Label>("Product Code:");
    codeLabel->set_xalign(0);
    auto* codeDisplay = Gtk::make_managed<Gtk::Label>(product.productCode);
    codeDisplay->set_xalign(0);
    codeDisplay->add_css_class("dim-label");
    grid->attach(*codeLabel, 0, 0);
    grid->attach(*codeDisplay, 1, 0);
    
    // Name (editable)
    auto* nameLabel = Gtk::make_managed<Gtk::Label>("Name:");
    nameLabel->set_xalign(0);
    auto* nameEntry = Gtk::make_managed<Gtk::Entry>();
    nameEntry->set_text(product.name);
    nameEntry->set_hexpand(true);
    grid->attach(*nameLabel, 0, 1);
    grid->attach(*nameEntry, 1, 1);
    
    // Status dropdown
    auto* statusLabel = Gtk::make_managed<Gtk::Label>("Status:");
    statusLabel->set_xalign(0);
    auto* statusCombo = Gtk::make_managed<Gtk::ComboBoxText>();
    statusCombo->append(config::defaults::kStatusActive);
    statusCombo->append(config::defaults::kStatusInactive);
    statusCombo->append(config::defaults::kStatusLowStock);
    if (product.status == config::defaults::kStatusActive) statusCombo->set_active(0);
    else if (product.status == config::defaults::kStatusInactive) statusCombo->set_active(1);
    else if (product.status == config::defaults::kStatusLowStock) statusCombo->set_active(2);
    grid->attach(*statusLabel, 0, 2);
    grid->attach(*statusCombo, 1, 2);
    
    // Stock
    auto* stockLabel = Gtk::make_managed<Gtk::Label>("Stock (units):");
    stockLabel->set_xalign(0);
    auto* stockSpin = Gtk::make_managed<Gtk::SpinButton>();
    stockSpin->set_range(0, 10000);
    stockSpin->set_increments(10, 100);
    stockSpin->set_value(product.stock);
    stockSpin->set_hexpand(true);
    grid->attach(*stockLabel, 0, 3);
    grid->attach(*stockSpin, 1, 3);
    
    // Quality Rate
    auto* qualityLabel = Gtk::make_managed<Gtk::Label>("Quality (%):");
    qualityLabel->set_xalign(0);
    auto* qualitySpin = Gtk::make_managed<Gtk::SpinButton>();
    qualitySpin->set_range(0.0, 100.0);
    qualitySpin->set_digits(1);
    qualitySpin->set_increments(0.1, 1.0);
    qualitySpin->set_value(product.qualityRate);
    qualitySpin->set_hexpand(true);
    grid->attach(*qualityLabel, 0, 4);
    grid->attach(*qualitySpin, 1, 4);
    
    dialog->get_content_area()->append(*grid);
    
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("Update", Gtk::ResponseType::OK);
    
    dialog->signal_response().connect([this, dialog, product, nameEntry, 
                                       statusCombo, stockSpin, qualitySpin]
                                      (int response) {
        if (response == Gtk::ResponseType::OK) {
            std::string name = nameEntry->get_text();
            std::string status = statusCombo->get_active_text();
            int stock = stockSpin->get_value_as_int();
            float quality = qualitySpin->get_value();
            
            if (!name.empty() && presenter_) {
                // ASYNC - non-blocking update
                presenter_->updateProduct(product.id, name, status, stock, quality, [this](bool success) {
                    // Callback runs on GTK main thread
                    if (!success) {
                        auto* parent = dynamic_cast<Gtk::Window*>(get_root());
                        dialogManager_.showError("Error", 
                                   "Failed to update product.",
                               parent);
                    }
                });
            }
        }
        delete dialog;
    });

    dialog->present();
}

void ProductsPage::applyStyles() {
    cssProvider_ = Gtk::CssProvider::create();
    cssProvider_->load_from_path(app::config::defaults::kProductsCSS);

    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(),
        cssProvider_,
        GTK_STYLE_PROVIDER_PRIORITY_USER
    );
}

}  // namespace app::view
