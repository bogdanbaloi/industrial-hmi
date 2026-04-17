#include "ProductsPage.h"
#include "src/gtk/view/DialogManager.h"
#include "src/gtk/view/ThemeManager.h"
#include "src/config/config_defaults.h"
#include "src/core/i18n.h"
#include "src/core/CsvSerializer.h"

#include <fstream>

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
    auto* title = Gtk::make_managed<Gtk::Label>(_("Products Database"));
    title->add_css_class("page-title");
    title->set_hexpand(true);
    title->set_xalign(0.0);
    toolbar->append(*title);
    
    // Add Product button
    addButton_ = Gtk::make_managed<Gtk::Button>(_("+ Add New Product"));
    addButton_->add_css_class("toolbar-button");
    addButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onAddProductClicked)
    );
    toolbar->append(*addButton_);
    
    // Refresh button
    refreshButton_ = Gtk::make_managed<Gtk::Button>(_("Refresh"));
    refreshButton_->add_css_class("toolbar-button");
    refreshButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onRefreshClicked)
    );
    toolbar->append(*refreshButton_);

    // Export CSV button
    exportButton_ = Gtk::make_managed<Gtk::Button>(_("Export CSV"));
    exportButton_->add_css_class("toolbar-button");
    exportButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onExportCsvClicked)
    );
    toolbar->append(*exportButton_);

    append(*toolbar);
}

void ProductsPage::buildSearchBar() {
    auto* searchBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    searchBox->set_margin_bottom(20);
    
    auto* searchLabel = Gtk::make_managed<Gtk::Label>(_("Search:"));
    searchLabel->add_css_class("search-label");
    searchBox->append(*searchLabel);
    
    searchEntry_ = Gtk::make_managed<Gtk::SearchEntry>();
    searchEntry_->set_placeholder_text(_("Search by Product ID or Name..."));
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
    columnView_->append_column(makeColumn(_("Product Code"), 120, false,
        [](const Glib::RefPtr<ProductObject>& p) { return p->getProductCode(); }));

    columnView_->append_column(makeColumn(_("Name"), 0, true,
        [](const Glib::RefPtr<ProductObject>& p) { return p->getName(); }));

    columnView_->append_column(makeColumn(_("Status"), 120, false,
        [](const Glib::RefPtr<ProductObject>& p) -> Glib::ustring {
            const auto& s = p->getStatus();
            if (s == config::defaults::kStatusActive)   return _("Active");
            if (s == config::defaults::kStatusInactive) return _("Inactive");
            if (s == config::defaults::kStatusLowStock) return _("Low Stock");
            return s;
        }));

    columnView_->append_column(makeColumn(_("Stock"), 80, false,
        [](const Glib::RefPtr<ProductObject>& p) { return std::to_string(p->getStock()); }));

    columnView_->append_column(makeColumn(_("Quality %"), 100, false,
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

    auto* viewButton = Gtk::make_managed<Gtk::Button>(_("View Details"));
    viewButton->add_css_class("toolbar-button");
    viewButton->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onViewProductClicked));
    actionBox->append(*viewButton);

    auto* editButton = Gtk::make_managed<Gtk::Button>(_("Edit"));
    editButton->add_css_class("toolbar-button");
    editButton->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onEditProductClicked));
    actionBox->append(*editButton);

    auto* deleteButton = Gtk::make_managed<Gtk::Button>(_("Delete"));
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
                                          _("Product Details"),
                                          false,
                                          Gtk::MessageType::INFO,
                                          Gtk::ButtonsType::OK);

    Glib::ustring message =
        Glib::ustring::compose(_("Product ID: %1"), vm.productId) + "\n\n" +
        vm.description + "\n\n" +
        Glib::ustring::compose(_("Created: %1"), vm.createdDate) + "\n" +
        Glib::ustring::compose(_("Status: %1"),
            vm.isVerified ? _("Active") : _("Inactive"));

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
    auto* dialog = new Gtk::Dialog(_("Add New Product"), *dynamic_cast<Gtk::Window*>(get_root()));
    dialog->set_default_size(400, 350);
    dialog->set_modal(true);
    app::view::ThemeManager::instance().applyToDialog(dialog);
    
    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(12);
    grid->set_column_spacing(12);
    grid->set_margin(20);
    
    // Product Code
    auto* codeLabel = Gtk::make_managed<Gtk::Label>(_("Product Code:"));
    codeLabel->set_xalign(0);
    auto* codeEntry = Gtk::make_managed<Gtk::Entry>();
    codeEntry->set_placeholder_text("PROD-007");
    codeEntry->set_hexpand(true);
    grid->attach(*codeLabel, 0, 0);
    grid->attach(*codeEntry, 1, 0);

    // Name
    auto* nameLabel = Gtk::make_managed<Gtk::Label>(_("Name:"));
    nameLabel->set_xalign(0);
    auto* nameEntry = Gtk::make_managed<Gtk::Entry>();
    nameEntry->set_placeholder_text(_("Product G"));
    nameEntry->set_hexpand(true);
    grid->attach(*nameLabel, 0, 1);
    grid->attach(*nameEntry, 1, 1);
    
    // Status dropdown
    auto* statusLabel = Gtk::make_managed<Gtk::Label>(_("Status:"));
    statusLabel->set_xalign(0);
    auto* statusCombo = Gtk::make_managed<Gtk::ComboBoxText>();
    statusCombo->append(config::defaults::kStatusActive, _("Active"));
    statusCombo->append(config::defaults::kStatusInactive, _("Inactive"));
    statusCombo->append(config::defaults::kStatusLowStock, _("Low Stock"));
    statusCombo->set_active(0);
    grid->attach(*statusLabel, 0, 2);
    grid->attach(*statusCombo, 1, 2);

    // Stock
    auto* stockLabel = Gtk::make_managed<Gtk::Label>(_("Stock (units):"));
    stockLabel->set_xalign(0);
    auto* stockSpin = Gtk::make_managed<Gtk::SpinButton>();
    stockSpin->set_range(0, 10000);
    stockSpin->set_increments(10, 100);
    stockSpin->set_value(100);
    stockSpin->set_hexpand(true);
    grid->attach(*stockLabel, 0, 3);
    grid->attach(*stockSpin, 1, 3);
    
    // Quality Rate
    auto* qualityLabel = Gtk::make_managed<Gtk::Label>(_("Quality (%):"));
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

    dialog->add_button(_("Cancel"), Gtk::ResponseType::CANCEL);
    dialog->add_button(_("Save"), Gtk::ResponseType::OK);

    dialog->signal_response().connect([this, dialog, codeEntry, nameEntry,
                                       statusCombo, stockSpin, qualitySpin]
                                      (int response) {
        if (response == Gtk::ResponseType::OK) {
            std::string code = codeEntry->get_text();
            std::string name = nameEntry->get_text();
            // Use the stable ID (kStatusActive/etc.) rather than the translated display text
            std::string status = statusCombo->get_active_id();
            if (status.empty()) status = config::defaults::kStatusActive;
            int stock = stockSpin->get_value_as_int();
            float quality = qualitySpin->get_value();

            if (!code.empty() && !name.empty() && presenter_) {
                // ASYNC - non-blocking database operation
                presenter_->addProduct(code, name, status, stock, quality, [this](bool success) {
                    // Callback runs on GTK main thread
                    if (!success) {
                        auto* parent = dynamic_cast<Gtk::Window*>(get_root());
                        dialogManager_.showError(_("Error"),
                                   _("Failed to add product. Product code may already exist."),
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
        _("Confirm Delete"),
        Glib::ustring::compose(_("Delete \"%1\"?\n\n"
                                 "This will mark the product as inactive (soft delete).\n"
                                 "The product will no longer appear in the list."),
                               productName),
        [this, productId](bool confirmed) {
            if (confirmed && presenter_) {
                // ASYNC - non-blocking soft delete
                presenter_->deleteProduct(productId, [this](bool success) {
                    // Callback runs on GTK main thread
                    if (!success) {
                        auto* parent = dynamic_cast<Gtk::Window*>(get_root());
                        dialogManager_.showError(_("Error"),
                                   _("Failed to delete product."),
                                   parent);
                    }
                });
            }
        },
        parent
    );
}

void ProductsPage::showEditProductDialog(const model::DatabaseManager::Product& product) {
    auto* dialog = new Gtk::Dialog(_("Edit Product"), *dynamic_cast<Gtk::Window*>(get_root()));
    dialog->set_default_size(400, 350);
    dialog->set_modal(true);
    app::view::ThemeManager::instance().applyToDialog(dialog);
    
    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(12);
    grid->set_column_spacing(12);
    grid->set_margin(20);
    
    // Product Code (read-only display)
    auto* codeLabel = Gtk::make_managed<Gtk::Label>(_("Product Code:"));
    codeLabel->set_xalign(0);
    auto* codeDisplay = Gtk::make_managed<Gtk::Label>(product.productCode);
    codeDisplay->set_xalign(0);
    codeDisplay->add_css_class("dim-label");
    grid->attach(*codeLabel, 0, 0);
    grid->attach(*codeDisplay, 1, 0);

    // Name (editable)
    auto* nameLabel = Gtk::make_managed<Gtk::Label>(_("Name:"));
    nameLabel->set_xalign(0);
    auto* nameEntry = Gtk::make_managed<Gtk::Entry>();
    nameEntry->set_text(product.name);
    nameEntry->set_hexpand(true);
    grid->attach(*nameLabel, 0, 1);
    grid->attach(*nameEntry, 1, 1);
    
    // Status dropdown
    auto* statusLabel = Gtk::make_managed<Gtk::Label>(_("Status:"));
    statusLabel->set_xalign(0);
    auto* statusCombo = Gtk::make_managed<Gtk::ComboBoxText>();
    statusCombo->append(config::defaults::kStatusActive, _("Active"));
    statusCombo->append(config::defaults::kStatusInactive, _("Inactive"));
    statusCombo->append(config::defaults::kStatusLowStock, _("Low Stock"));
    if (product.status == config::defaults::kStatusActive) statusCombo->set_active_id(config::defaults::kStatusActive);
    else if (product.status == config::defaults::kStatusInactive) statusCombo->set_active_id(config::defaults::kStatusInactive);
    else if (product.status == config::defaults::kStatusLowStock) statusCombo->set_active_id(config::defaults::kStatusLowStock);
    grid->attach(*statusLabel, 0, 2);
    grid->attach(*statusCombo, 1, 2);

    // Stock
    auto* stockLabel = Gtk::make_managed<Gtk::Label>(_("Stock (units):"));
    stockLabel->set_xalign(0);
    auto* stockSpin = Gtk::make_managed<Gtk::SpinButton>();
    stockSpin->set_range(0, 10000);
    stockSpin->set_increments(10, 100);
    stockSpin->set_value(product.stock);
    stockSpin->set_hexpand(true);
    grid->attach(*stockLabel, 0, 3);
    grid->attach(*stockSpin, 1, 3);
    
    // Quality Rate
    auto* qualityLabel = Gtk::make_managed<Gtk::Label>(_("Quality (%):"));
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

    dialog->add_button(_("Cancel"), Gtk::ResponseType::CANCEL);
    dialog->add_button(_("Update"), Gtk::ResponseType::OK);

    dialog->signal_response().connect([this, dialog, product, nameEntry,
                                       statusCombo, stockSpin, qualitySpin]
                                      (int response) {
        if (response == Gtk::ResponseType::OK) {
            std::string name = nameEntry->get_text();
            // Use the stable ID so DB stores the canonical (untranslated) status
            std::string status = statusCombo->get_active_id();
            if (status.empty()) status = product.status;
            int stock = stockSpin->get_value_as_int();
            float quality = qualitySpin->get_value();

            if (!name.empty() && presenter_) {
                // ASYNC - non-blocking update
                presenter_->updateProduct(product.id, name, status, stock, quality, [this](bool success) {
                    // Callback runs on GTK main thread
                    if (!success) {
                        auto* parent = dynamic_cast<Gtk::Window*>(get_root());
                        dialogManager_.showError(_("Error"),
                                   _("Failed to update product."),
                               parent);
                    }
                });
            }
        }
        delete dialog;
    });

    dialog->present();
}

void ProductsPage::onExportCsvClicked() {
    if (!presenter_) return;

    // Fetch products asynchronously; callback runs on GTK main thread.
    presenter_->exportProducts([this](std::vector<model::Product> products) {
        const std::string path = "products.csv";
        exportToCsv(path, products);
    });
}

void ProductsPage::exportToCsv(const std::string& path,
                                const std::vector<model::Product>& products) {
    std::vector<std::string> header = {
        _("Product Code"), _("Name"), _("Status"),
        _("Stock"), _("Quality %")
    };

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        auto* parent = dynamic_cast<Gtk::Window*>(get_root());
        dialogManager_.showError(
            _("Error"),
            _("Could not write the CSV file."),
            parent);
        return;
    }

    app::core::CsvSerializer::write(out, products, header);
    out.close();

    auto* parent = dynamic_cast<Gtk::Window*>(get_root());
    dialogManager_.showInfo(
        _("Export Complete"),
        Glib::ustring::compose(
            _("Products exported to:\n%1"), path),
        parent);
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
