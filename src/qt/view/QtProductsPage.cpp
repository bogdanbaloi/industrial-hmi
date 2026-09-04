// ProductsPresenter.h (and the model headers it pulls) before any Qt header, so
// their ERROR enumerators are parsed before wingdi.h defines the ERROR macro.
#include "src/presenter/ProductsPresenter.h"

#include "src/qt/view/QtProductsPage.h"

#include "ui_QtProductsPage.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>

namespace app::view {

namespace {
constexpr int kColumnCount     = 5;
constexpr int kQualityDecimals = 1;
}  // namespace

QtProductsPage::QtProductsPage(ProductsPresenter& presenter, QWidget* parent)
    : QWidget(parent),
      presenter_(presenter),
      ui_(std::make_unique<Ui::QtProductsPage>()) {
    ui_->setupUi(this);

    ui_->productsTable->setColumnCount(kColumnCount);
    ui_->productsTable->setHorizontalHeaderLabels(
        {tr("Code"), tr("Name"), tr("Status"), tr("Stock"), tr("Quality %")});
    ui_->productsTable->horizontalHeader()->setStretchLastSection(true);
    ui_->productsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui_->productsTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    connect(ui_->refreshButton, &QPushButton::clicked, this,
            [this] { presenter_.loadProducts(); });
}

QtProductsPage::~QtProductsPage() = default;

void QtProductsPage::onProductsLoaded(const presenter::ProductsViewModel& vm) {
    auto* table = ui_->productsTable;
    table->setRowCount(static_cast<int>(vm.products.size()));

    int row = 0;
    for (const auto& product : vm.products) {
        table->setItem(row, 0,
                       new QTableWidgetItem(
                           QString::fromStdString(product.productCode)));
        table->setItem(
            row, 1, new QTableWidgetItem(QString::fromStdString(product.name)));
        table->setItem(row, 2,
                       new QTableWidgetItem(
                           QString::fromStdString(product.status)));
        table->setItem(row, 3,
                       new QTableWidgetItem(QString::number(product.stock)));
        table->setItem(
            row, 4,
            new QTableWidgetItem(
                QString::number(product.qualityRate, 'f', kQualityDecimals)));
        ++row;
    }
}

}  // namespace app::view
