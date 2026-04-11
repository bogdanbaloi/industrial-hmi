#include "ProductsPage.h"

namespace app::view {

ProductsPage::ProductsPage()
    : Gtk::Box(Gtk::Orientation::VERTICAL) {
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
    // Create ListStore
    columns_.add(columns_.id);
    columns_.add(columns_.productId);
    columns_.add(columns_.description);
    columns_.add(columns_.status);
    
    listStore_ = Gtk::ListStore::create(columns_);
    
    // Create TreeView
    treeView_ = Gtk::make_managed<Gtk::TreeView>(listStore_);
    treeView_->set_hexpand(true);
    treeView_->set_vexpand(true);
    
    // Add columns
    treeView_->append_column("ID", columns_.productId);
    treeView_->append_column("Description", columns_.description);
    treeView_->append_column("Status", columns_.status);
    
    // Configure columns
    auto* col1 = treeView_->get_column(0);
    col1->set_fixed_width(150);
    col1->set_resizable(true);
    
    auto* col2 = treeView_->get_column(1);
    col2->set_expand(true);
    col2->set_resizable(true);
    
    auto* col3 = treeView_->get_column(2);
    col3->set_fixed_width(100);
    col3->set_resizable(true);
    
    // Row activation (double-click)
    treeView_->signal_row_activated().connect([this](const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn*) {
        onProductSelected(path);
    });
    
    // ScrolledWindow
    scrolledWindow_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    scrolledWindow_->set_child(*treeView_);
    scrolledWindow_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scrolledWindow_->set_vexpand(true);
    
    append(*scrolledWindow_);
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

void ProductsPage::onProductSelected(const Gtk::TreeModel::Path& path) {
    auto iter = listStore_->get_iter(path);
    if (iter) {
        int id = (*iter)[columns_.id];
        if (presenter_) {
            presenter_->viewProduct(id);
        }
    }
}

// Helper Methods
void ProductsPage::updateProductsList(const presenter::ProductsViewModel& vm) {
    listStore_->clear();
    
    for (const auto& product : vm.products) {
        auto row = *(listStore_->append());
        row[columns_.id] = product.id;
        row[columns_.productId] = product.productId;
        row[columns_.description] = product.description;
        row[columns_.status] = product.isVerified ? "Active" : "Inactive";
    }
}

void ProductsPage::showProductDetail(const presenter::ViewProductDialogViewModel& vm) {
    // Create detail dialog
    auto dialog = Gtk::MessageDialog(*dynamic_cast<Gtk::Window*>(get_root()),
                                     "Product Details",
                                     false,
                                     Gtk::MessageType::INFO,
                                     Gtk::ButtonsType::OK);
    
    std::string message = "Product ID: " + vm.productId + "\n\n" +
                         vm.description + "\n\n" +
                         "Created: " + vm.createdDate + "\n" +
                         "Status: " + (vm.isVerified ? "Active" : "Inactive");
    
    dialog.set_secondary_text(message);
    dialog.run();
}

void ProductsPage::applyStyles() {
    cssProvider_ = Gtk::CssProvider::create();
    
    cssProvider_->load_from_data(R"(
        .page-title {
            font-size: 24px;
            font-weight: 700;
            color: #263238;
        }
        
        .toolbar-button {
            font-size: 14px;
            font-weight: 500;
            min-width: 100px;
            min-height: 40px;
            border-radius: 6px;
        }
        
        .search-label {
            font-size: 14px;
            font-weight: 500;
            color: #546E7A;
        }
    )");
    
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(),
        cssProvider_,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
}

}  // namespace app::view
