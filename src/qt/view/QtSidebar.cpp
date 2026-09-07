#include "src/qt/view/QtSidebar.h"

#include "src/qt/view/QtIcons.h"

#include <QApplication>
#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>

#include <cstddef>
#include <utility>

namespace app::view {

namespace {
constexpr int kSidebarWidth = 220;
constexpr int kNavSpacing   = 4;
constexpr int kNavIconSize  = 18;
constexpr int kBrandSpacing = 8;
}  // namespace

QtSidebar::QtSidebar(SelectCallback onSelect, QWidget* parent)
    : QWidget(parent), onSelect_(std::move(onSelect)) {
    setObjectName("sidebar");
    setFixedWidth(kSidebarWidth);

    auto* root = new QVBoxLayout(this);

    // Brand row: logo + title, mirroring the GTK sidebar's app-logo + title.
    auto* brandRow    = new QWidget(this);
    auto* brandLayout = new QHBoxLayout(brandRow);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(kBrandSpacing);

    auto* logo = new QLabel(brandRow);
    logo->setPixmap(icons::appLogo());
    brandLayout->addWidget(logo);

    brandLabel_ = new QLabel(tr("Industrial HMI"), brandRow);
    brandLabel_->setObjectName("sidebarBrand");
    brandLayout->addWidget(brandLabel_);
    brandLayout->addStretch();

    root->addWidget(brandRow);

    navLayout_ = new QVBoxLayout();
    navLayout_->setSpacing(kNavSpacing);
    root->addLayout(navLayout_);

    root->addStretch(1);

    userLabel_ = new QLabel(tr("Bogdan B. · Operations"), this);
    userLabel_->setObjectName("sidebarUser");
    root->addWidget(userLabel_);

    // Kiosk mode has no title bar, so the shell provides its own quit control.
    quitButton_ = new QPushButton(tr("Exit application"), this);
    quitButton_->setObjectName("sidebarQuit");
    quitButton_->setIcon(icons::quit());
    quitButton_->setIconSize(QSize(kNavIconSize, kNavIconSize));
    connect(quitButton_, &QPushButton::clicked, qApp, &QCoreApplication::quit);
    root->addWidget(quitButton_);

    group_ = new QButtonGroup(this);
    group_->setExclusive(true);
    connect(group_, &QButtonGroup::idClicked, this, [this](int id) {
        if (onSelect_) {
            onSelect_(id);
        }
    });
}

int QtSidebar::addItem(const QString& label, const QIcon& icon) {
    auto* button = new QPushButton(label, this);
    button->setCheckable(true);
    if (!icon.isNull()) {
        button->setIcon(icon);
        button->setIconSize(QSize(kNavIconSize, kNavIconSize));
    }
    const int index = count_++;
    group_->addButton(button, index);
    navLayout_->addWidget(button);

    // A count badge parented to the button, hidden until setBadge shows it.
    auto* badge = new QLabel(button);
    badge->setObjectName("navBadge");
    badge->hide();
    badges_.push_back(badge);
    return index;
}

void QtSidebar::setItemLabel(int index, const QString& label) {
    if (auto* button = group_->button(index)) {
        button->setText(label);
    }
}

void QtSidebar::changeEvent(QEvent* event) {
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        brandLabel_->setText(tr("Industrial HMI"));
        userLabel_->setText(tr("Bogdan B. · Operations"));
        quitButton_->setText(tr("Exit application"));
    }
    QWidget::changeEvent(event);
}

void QtSidebar::select(int index) {
    if (auto* button = group_->button(index)) {
        button->setChecked(true);
    }
}

void QtSidebar::setBadge(int index, int count) {
    if (index < 0 || static_cast<std::size_t>(index) >= badges_.size()) {
        return;
    }
    auto* badge = badges_[index];
    if (count <= 0) {
        badge->hide();
        return;
    }
    constexpr int kBadgeMargin = 8;
    badge->setText(QString::number(count));
    badge->adjustSize();
    if (auto* button = group_->button(index)) {
        badge->move(button->width() - badge->width() - kBadgeMargin,
                    (button->height() - badge->height()) / 2);
    }
    badge->show();
    badge->raise();
}

}  // namespace app::view
