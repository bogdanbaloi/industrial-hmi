#pragma once

#include <QIcon>
#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

class QButtonGroup;
class QLabel;
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

    /// Append a nav entry; returns its index. `icon` is an optional SVG icon
    /// shown before the label.
    int addItem(const QString& label, const QIcon& icon = QIcon());

    /// Check a nav entry without firing the callback (initial selection).
    void select(int index);

    /// Show a count badge on a nav entry (e.g. active alerts); count <= 0 hides
    /// it.
    void setBadge(int index, int count);

private:
    SelectCallback       onSelect_;
    QButtonGroup*        group_{nullptr};
    QVBoxLayout*         navLayout_{nullptr};
    std::vector<QLabel*> badges_;
    int                  count_{0};
};

}  // namespace app::view
