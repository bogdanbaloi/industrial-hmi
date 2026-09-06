#include "src/qt/view/QtSidebar.h"

#include <QButtonGroup>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace app::view {

namespace {
constexpr int kSidebarWidth = 220;
constexpr int kNavSpacing   = 4;
}  // namespace

QtSidebar::QtSidebar(SelectCallback onSelect, QWidget* parent)
    : QWidget(parent), onSelect_(std::move(onSelect)) {
    setObjectName("sidebar");
    setFixedWidth(kSidebarWidth);

    auto* root = new QVBoxLayout(this);

    auto* brand = new QLabel(tr("Nordwind SC"), this);
    brand->setObjectName("sidebarBrand");
    root->addWidget(brand);

    navLayout_ = new QVBoxLayout();
    navLayout_->setSpacing(kNavSpacing);
    root->addLayout(navLayout_);

    root->addStretch(1);

    auto* user = new QLabel(tr("Bogdan B. · Operations"), this);
    user->setObjectName("sidebarUser");
    root->addWidget(user);

    group_ = new QButtonGroup(this);
    group_->setExclusive(true);
    connect(group_, &QButtonGroup::idClicked, this, [this](int id) {
        if (onSelect_) {
            onSelect_(id);
        }
    });
}

int QtSidebar::addItem(const QString& label) {
    auto* button = new QPushButton(label, this);
    button->setCheckable(true);
    const int index = count_++;
    group_->addButton(button, index);
    navLayout_->addWidget(button);
    return index;
}

void QtSidebar::select(int index) {
    if (auto* button = group_->button(index)) {
        button->setChecked(true);
    }
}

}  // namespace app::view
