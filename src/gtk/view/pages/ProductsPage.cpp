#include "ProductsPage.h"
#include "src/gtk/view/DialogManager.h"
#include "src/gtk/view/ThemeManager.h"
#include "src/gtk/view/css_classes.h"
#include "src/gtk/view/ui_sizes.h"
#include "src/config/config_defaults.h"
#include "src/core/i18n.h"
#include "src/core/CsvSerializer.h"
#include "src/core/Application.h"

#include <format>
#include <fstream>

namespace app::view {

namespace {
using namespace app::view::sizes;
inline app::core::Logger& log() {
    return app::core::Application::instance().logger();
}
}  // namespace

ProductsPage::ProductsPage(DialogManager& dialogManager)
    : Page(dialogManager) {
    buildUI();
    applyStyles();
}

ProductsPage::~ProductsPage() {
    if (presenter_) {
        presenter_->removeObserver(this);
    }
}

Glib::ustring ProductsPage::pageTitle() const {
    return _("Products Database");
}

void ProductsPage::initialize(std::shared_ptr<ProductsPresenter> presenter) {
    log().info("ProductsPage: initialized, registering with presenter");
    presenter_ = presenter;
    presenter_->addObserver(this);
    presenter_->initialize();
}

// ViewObserver implementation
void ProductsPage::onProductsLoaded(const presenter::ProductsViewModel& vm) {
    log().trace("ProductsPage: ProductsViewModel received ({} products)",
                vm.products.size());
    Glib::signal_idle().connect_once([this, vm]() {
        updateProductsList(vm);
    });
}

void ProductsPage::onViewProductReady(const presenter::ViewProductDialogViewModel& vm) {
    log().trace("ProductsPage: ViewProductDialogViewModel received ({})", vm.productId);
    Glib::signal_idle().connect_once([this, vm]() {
        showProductDetail(vm);
    });
}

// UI Construction — static layout from XML, dynamic ColumnView from code.
void ProductsPage::buildUI() {
    auto builder = Gtk::Builder::create_from_file(
        app::config::defaults::kProductsPageUI);

    auto* root = builder->get_widget<Gtk::Box>("products_root");
    if (root) append(*root);

    // ---- Toolbar buttons ----
    addButton_ = builder->get_widget<Gtk::Button>("btn_add");
    refreshButton_ = builder->get_widget<Gtk::Button>("btn_refresh");
    exportButton_ = builder->get_widget<Gtk::Button>("btn_export");

    addButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onAddProductClicked));
    refreshButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onRefreshClicked));
    exportButton_->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onExportCsvClicked));

    // ---- Search ----
    searchEntry_ = builder->get_widget<Gtk::SearchEntry>("search_entry");
    searchEntry_->signal_search_changed().connect(
        sigc::mem_fun(*this, &ProductsPage::onSearchChanged));

    // ---- ColumnView (programmatic — factories + list model can't live in XML) ----
    listStore_ = Gio::ListStore<ProductObject>::create();
    selectionModel_ = Gtk::SingleSelection::create(listStore_);

    columnView_ = Gtk::make_managed<Gtk::ColumnView>(selectionModel_);
    columnView_->set_hexpand(true);
    columnView_->set_vexpand(true);
    columnView_->set_show_column_separators(true);
    columnView_->set_show_row_separators(true);

    auto makeColumn = [](const Glib::ustring& title, int fixedWidth, bool expand,
                         std::function<Glib::ustring(const Glib::RefPtr<ProductObject>&)> getter) {
        auto factory = Gtk::SignalListItemFactory::create();
        factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto* label = Gtk::make_managed<Gtk::Label>();
            label->set_xalign(0.0);
            label->set_margin_start(kSpacingSmall);
            label->set_margin_end(kSpacingSmall);
            item->set_child(*label);
        });
        factory->signal_bind().connect([getter](const Glib::RefPtr<Gtk::ListItem>& item) {
            auto obj = std::dynamic_pointer_cast<ProductObject>(item->get_item());
            auto* label = dynamic_cast<Gtk::Label*>(item->get_child());
            if (obj && label) label->set_text(getter(obj));
        });
        auto column = Gtk::ColumnViewColumn::create(title, factory);
        if (fixedWidth > 0) column->set_fixed_width(fixedWidth);
        if (expand) column->set_expand(true);
        column->set_resizable(true);
        return column;
    };

    columnView_->append_column(makeColumn(_("Product Code"), kProductsColumnCodeWidth, false,
        [](const Glib::RefPtr<ProductObject>& p) { return p->getProductCode(); }));
    columnView_->append_column(makeColumn(_("Name"), 0, true,
        [](const Glib::RefPtr<ProductObject>& p) { return p->getName(); }));
    columnView_->append_column(makeColumn(_("Status"), kProductsColumnStatusWidth, false,
        [](const Glib::RefPtr<ProductObject>& p) -> Glib::ustring {
            const auto& s = p->getStatus();
            if (s == config::defaults::kStatusActive)   return _("Active");
            if (s == config::defaults::kStatusInactive) return _("Inactive");
            if (s == config::defaults::kStatusLowStock) return _("Low Stock");
            return s;
        }));
    columnView_->append_column(makeColumn(_("Stock"), kProductsColumnStockWidth, false,
        [](const Glib::RefPtr<ProductObject>& p) { return std::to_string(p->getStock()); }));
    columnView_->append_column(makeColumn(_("Quality %"), kProductsColumnQualityWidth, false,
        [](const Glib::RefPtr<ProductObject>& p) {
            const float rate = p->getQualityRate();
            return Glib::ustring(
                std::vformat("{:.1f}%", std::make_format_args(rate)));
        }));

    columnView_->signal_activate().connect(
        sigc::mem_fun(*this, &ProductsPage::onProductActivated));

    // Inject ColumnView into the ScrolledWindow defined in XML
    scrolledWindow_ = builder->get_widget<Gtk::ScrolledWindow>("table_container");
    scrolledWindow_->set_child(*columnView_);

    // ---- Action buttons ----
    auto* viewBtn = builder->get_widget<Gtk::Button>("btn_view");
    auto* editBtn = builder->get_widget<Gtk::Button>("btn_edit");
    auto* deleteBtn = builder->get_widget<Gtk::Button>("btn_delete");

    viewBtn->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onViewProductClicked));
    editBtn->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onEditProductClicked));
    deleteBtn->signal_clicked().connect(
        sigc::mem_fun(*this, &ProductsPage::onDeleteProductClicked));
}

// Event Handlers
void ProductsPage::onSearchChanged() {
    if (presenter_) {
        std::string query = searchEntry_->get_text();
        log().trace("ProductsPage: search text changed -> \"{}\"", query);
        presenter_->searchProducts(query);
    }
}

void ProductsPage::onRefreshClicked() {
    log().debug("ProductsPage: Refresh button clicked");
    if (presenter_) {
        searchEntry_->set_text("");
        presenter_->loadProducts();
    }
}

void ProductsPage::onProductActivated(guint position) {
    log().debug("ProductsPage: row activated at position {}", position);
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
    log().debug("ProductsPage: Add button clicked");
    showAddProductDialog();
}

void ProductsPage::onViewProductClicked() {
    int productId = getSelectedProductId();
    log().debug("ProductsPage: View button clicked (selected id={})", productId);
    if (productId != config::defaults::kInvalidProductId && presenter_) {
        presenter_->viewProduct(productId);
    }
}

void ProductsPage::onDeleteProductClicked() {
    int productId = getSelectedProductId();
    log().debug("ProductsPage: Delete button clicked (selected id={})", productId);
    if (productId != config::defaults::kInvalidProductId && presenter_) {
        auto product = presenter_->getProduct(productId);
        if (product.id != config::defaults::kInvalidProductId) {
            showDeleteConfirmDialog(productId, product.name);
        }
    }
}

void ProductsPage::onEditProductClicked() {
    int productId = getSelectedProductId();
    log().debug("ProductsPage: Edit button clicked (selected id={})", productId);
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
    dialog->set_default_size(kFormDialogWidth, kFormDialogHeight);
    dialog->set_modal(true);
    app::view::ThemeManager::instance().applyToDialog(dialog);

    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(kSpacingMedium);
    grid->set_column_spacing(kSpacingMedium);
    grid->set_margin(kSpacingLarge);
    
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
    stockSpin->set_range(kStockSpinMin, kStockSpinMax);
    stockSpin->set_increments(kStockSpinStep, kStockSpinStep * kStockSpinStep);
    constexpr int kAddDialogDefaultStock = 100;
    stockSpin->set_value(kAddDialogDefaultStock);
    stockSpin->set_hexpand(true);
    grid->attach(*stockLabel, 0, 3);
    grid->attach(*stockSpin, 1, 3);
    
    // Quality Rate
    auto* qualityLabel = Gtk::make_managed<Gtk::Label>(_("Quality (%):"));
    qualityLabel->set_xalign(0);
    auto* qualitySpin = Gtk::make_managed<Gtk::SpinButton>();
    qualitySpin->set_range(kQualitySpinMin, kQualitySpinMax);
    qualitySpin->set_digits(1);
    qualitySpin->set_increments(kQualitySpinStep, 1.0);
    qualitySpin->set_value(config::defaults::kQualityPassThreshold);
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
    dialog->set_default_size(kFormDialogWidth, kFormDialogHeight);
    dialog->set_modal(true);
    app::view::ThemeManager::instance().applyToDialog(dialog);

    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(kSpacingMedium);
    grid->set_column_spacing(kSpacingMedium);
    grid->set_margin(kSpacingLarge);
    
    // Product Code (read-only display)
    auto* codeLabel = Gtk::make_managed<Gtk::Label>(_("Product Code:"));
    codeLabel->set_xalign(0);
    auto* codeDisplay = Gtk::make_managed<Gtk::Label>(product.productCode);
    codeDisplay->set_xalign(0);
    codeDisplay->add_css_class(css::kDimLabel);
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
    stockSpin->set_range(kStockSpinMin, kStockSpinMax);
    stockSpin->set_increments(kStockSpinStep, kStockSpinStep * kStockSpinStep);
    stockSpin->set_value(product.stock);
    stockSpin->set_hexpand(true);
    grid->attach(*stockLabel, 0, 3);
    grid->attach(*stockSpin, 1, 3);
    
    // Quality Rate
    auto* qualityLabel = Gtk::make_managed<Gtk::Label>(_("Quality (%):"));
    qualityLabel->set_xalign(0);
    auto* qualitySpin = Gtk::make_managed<Gtk::SpinButton>();
    qualitySpin->set_range(kQualitySpinMin, kQualitySpinMax);
    qualitySpin->set_digits(1);
    qualitySpin->set_increments(kQualitySpinStep, 1.0);
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
    log().debug("ProductsPage: Export CSV button clicked");
    if (!presenter_) return;

    // Fetch products asynchronously; callback runs on GTK main thread.
    presenter_->exportProducts([this](std::vector<model::Product> products) {
        const std::string path = "products.csv";
        log().info("ProductsPage: Exporting {} products to {}", products.size(), path);
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
