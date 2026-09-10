#pragma once

// ViewObserver.h before any Qt header (wingdi ERROR macro vs
// StatusZoneViewModel::Severity::ERROR pulled transitively).
#include "src/presenter/ViewObserver.h"

#include <QWidget>

#include <memory>

class QEvent;

// The Ui namespace name is fixed by Qt's uic generator, not our style.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace Ui {
class QtProductsPage;
}

namespace app {
class ProductsPresenter;
}

namespace app::view {

/// Products page: a passive QWidget + app::ViewObserver that renders the
/// products table from the real ProductsPresenter, the same seam the GTK
/// ProductsPage and the console view use. The Refresh button forwards to the
/// presenter's loadProducts(); onProductsLoaded arrives synchronously on the UI
/// thread (sync repository path), so the table is filled directly.
class QtProductsPage : public QWidget, public app::ViewObserver {
public:
    explicit QtProductsPage(ProductsPresenter& presenter,
                            QWidget* parent = nullptr);
    ~QtProductsPage() override;

    QtProductsPage(const QtProductsPage&)            = delete;
    QtProductsPage& operator=(const QtProductsPage&) = delete;
    QtProductsPage(QtProductsPage&&)                 = delete;
    QtProductsPage& operator=(QtProductsPage&&)      = delete;

    void onProductsLoaded(const presenter::ProductsViewModel& vm) override;

protected:
    void changeEvent(QEvent* event) override;

private:
    void applyHeaderLabels();

    ProductsPresenter&                  presenter_;
    std::unique_ptr<Ui::QtProductsPage> ui_;
};

}  // namespace app::view
