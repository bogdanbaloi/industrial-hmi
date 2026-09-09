#include "src/qt/view/QtMultiStationPage.h"

#include "src/qt/view/QtDashboardPage.h"

#include "ui_QtMultiStationPage.h"

#include <QEvent>

namespace app::view {

QtMultiStationPage::QtMultiStationPage(DashboardPresenter& primaryPresenter,
                                       DashboardPresenter& secondaryPresenter,
                                       QWidget* parent)
    : QWidget(parent),
      ui_(std::make_unique<Ui::QtMultiStationPage>()) {
    ui_->setupUi(this);

    // Each pane is a full dashboard over its own presenter. The composition root
    // wires them as observers after construction.
    primaryPane_   = new QtDashboardPage(primaryPresenter);
    secondaryPane_ = new QtDashboardPage(secondaryPresenter);
    ui_->primaryLayout->addWidget(primaryPane_);
    ui_->secondaryLayout->addWidget(secondaryPane_);
}

QtMultiStationPage::~QtMultiStationPage() = default;

void QtMultiStationPage::changeEvent(QEvent* event) {
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        ui_->retranslateUi(this);
    }
    QWidget::changeEvent(event);
}

}  // namespace app::view
