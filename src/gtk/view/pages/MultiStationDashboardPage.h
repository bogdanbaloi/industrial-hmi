#pragma once

#include "src/auth/Role.h"
#include "src/gtk/view/pages/DashboardPage.h"
#include "src/gtk/view/pages/Page.h"
#include "src/presenter/DashboardPresenter.h"

#include <gtkmm.h>
#include <memory>

namespace app::view {

class DialogManager;

/// Notebook tab that hosts two DashboardPage instances side by side --
/// one for the primary station, one for the secondary. Used in multi-station
/// deployments where a calibration / fill station feeds a production
/// station, both supervised from a single HMI terminal.
///
/// @design Composition over duplication. Each pane is a full
///         DashboardPage with its own DashboardPresenter; the two are
///         linked at the model layer via PrimaryToSecondaryBridge (see
///         src/integration/PrimaryToSecondaryBridge.h and ADR-0011), not
///         through this view class. The view's only job is to lay them
///         out and forward lifecycle hooks.
///
/// @pattern MVP -- this is a thin View shell around two View instances.
///          No business logic, no presenter coordination, nothing that
///          two independent DashboardPage tabs wouldn't do on their own.
///          The bridge happens below.
///
/// @thread_safety Hooks are called on the GTK main thread; each
///                contained DashboardPage handles its own observer
///                marshalling for its own presenter.
class MultiStationDashboardPage : public Page {
public:
    /// One DialogManager is shared by both panes -- there's only one
    /// operator + one window, so dialog modality stays simple.
    explicit MultiStationDashboardPage(DialogManager& dialogManager);

    ~MultiStationDashboardPage() override = default;

    /// Bind the two presenters. Must be called once before the page is
    /// shown. The page does NOT own the presenters; lifetime is the
    /// composition root's responsibility (see main.cpp).
    void initialize(std::shared_ptr<DashboardPresenter> primaryPresenter,
                    std::shared_ptr<DashboardPresenter> secondaryPresenter);

    /// Apply the same role gating to both panes -- if the operator
    /// can't trigger Reset on the primary, they shouldn't on the secondary
    /// either. Matches DashboardPage::applyRole semantics.
    void applyRole(app::auth::Role role);

    // Page overrides -- delegated to both child pages.
    [[nodiscard]] Glib::ustring pageTitle() const override;
    void onThemeChanged() override;
    void onLanguageChanged() override;

    /// Forwarded to both panes; same surface as DashboardPage so
    /// MainWindow's themed-widget redraw hook can reach the gauges.
    void refreshThemedWidgets();

private:
    /// Construct the horizontal split + the two DashboardPage children
    /// + the per-pane "MASTER" / "SLAVE" headers. Called from the
    /// constructor.
    void buildUi();

    // make_managed-owned children, set during buildUi(). Parent is the
    // page's own Gtk::Box so GTK handles destruction.
    DashboardPage* primaryPage_{nullptr};
    DashboardPage* secondaryPage_{nullptr};
};

}  // namespace app::view
