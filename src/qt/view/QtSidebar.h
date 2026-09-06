#pragma once

#include <QString>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QVBoxLayout;

namespace app::view {

/// Custom navigation sidebar: a branded rail of exclusive nav buttons plus a
/// user footer. It owns no pages and knows no page types; it reports the
/// selected index through a callback (DIP), so the shell decides what each
/// index shows. SRP: navigation only.
class QtSidebar : public QWidget {
public:
    using SelectCallback = std::function<void(int)>;

    explicit QtSidebar(SelectCallback onSelect, QWidget* parent = nullptr);

    /// Append a nav entry; returns its index.
    int addItem(const QString& label);

    /// Check a nav entry without firing the callback (initial selection).
    void select(int index);

private:
    SelectCallback onSelect_;
    QButtonGroup*  group_{nullptr};
    QVBoxLayout*   navLayout_{nullptr};
    int            count_{0};
};

}  // namespace app::view
