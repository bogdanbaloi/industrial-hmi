#pragma once

#include <QIcon>
#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

class QButtonGroup;
class QEvent;
class QLabel;
class QPushButton;
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

    /// Re-set a nav entry's label (the shell drives this on a live language
    /// change, since it owns the source strings); no-op if the index is unknown.
    void setItemLabel(int index, const QString& label);

    /// Check a nav entry without firing the callback (initial selection).
    void select(int index);

    /// Show a count badge on a nav entry (e.g. active alerts); count <= 0 hides
    /// it.
    void setBadge(int index, int count);

    /// Replace the footer identity text (the signed-in user + role). Once set,
    /// a language change leaves it alone -- a user's name + role code is not a
    /// translatable string.
    void setUserText(const QString& text);

    /// Reveal the "Sign out" control and route its clicks to `handler`. Only
    /// called when auth is enabled; the button stays hidden otherwise.
    void enableSignOut(std::function<void()> handler);

    /// Reveal the "Change password" control and route its clicks to `handler`.
    /// Only called for an authenticated session with a users presenter.
    void enableChangePassword(std::function<void()> handler);

protected:
    /// Retranslate the sidebar's own static text (brand / user / quit) on a
    /// live language change. Nav labels are re-set by the shell via
    /// setItemLabel (it owns their source strings).
    void changeEvent(QEvent* event) override;

private:
    SelectCallback       onSelect_;
    QButtonGroup*        group_{nullptr};
    QVBoxLayout*         navLayout_{nullptr};
    std::vector<QLabel*> badges_;
    QLabel*              brandLabel_{nullptr};
    QLabel*              userLabel_{nullptr};
    QPushButton*         changePasswordButton_{nullptr};
    QPushButton*         signOutButton_{nullptr};
    QPushButton*         quitButton_{nullptr};
    int                  count_{0};
};

}  // namespace app::view
