#pragma once

#include "src/presenter/BasePresenter.h"

namespace app::integration { class IntegrationManager; }

namespace app {

/// Polls the integration-manager's backends and pushes the snapshot
/// to subscribed Views as a `BackendHealthViewModel`.
///
/// Why polling (and not push-from-backend)? Backends already implement
/// state via cheap atomic loads -- adding a notification fan-out into
/// the manager would couple the integration layer to the presenter
/// layer (Backend would have to know about ViewObserver, breaking the
/// inverted dependency). A 2 Hz poll is plenty for a status dot.
///
/// SOLID:
///   * S -- one job: convert a manager's live state to a ViewModel.
///     No backend lifecycle (manager owns that), no UI (the bar widget
///     receives the ViewModel and renders it), no domain logic.
///   * O -- a new backend type contributes its name + state through
///     `IntegrationBackend` overrides. This presenter doesn't change.
///   * L -- inherits `BasePresenter`; `IntegrationManager` is the
///     dependency, never a concrete backend.
///   * I -- emits a single ViewObserver hook (`onBackendHealthChanged`).
///   * D -- depends on the manager + backend interfaces only.
///
/// Threading: `poll()` runs on whichever thread the caller drives the
/// timer from. The View layer marshals via Glib::signal_idle as it
/// does for every other presenter callback.
///
/// Rule of 5: copy / move deleted (BasePresenter constraint -- the
/// observer list has a mutex member).
class BackendHealthPresenter : public BasePresenter {
public:
    explicit BackendHealthPresenter(integration::IntegrationManager& manager);

    ~BackendHealthPresenter() override = default;

    BackendHealthPresenter(const BackendHealthPresenter&)            = delete;
    BackendHealthPresenter& operator=(const BackendHealthPresenter&) = delete;
    BackendHealthPresenter(BackendHealthPresenter&&)                 = delete;
    BackendHealthPresenter& operator=(BackendHealthPresenter&&)      = delete;

    /// BasePresenter declares this pure-virtual so derived classes
    /// can subscribe to model signals at construction. We have nothing
    /// to subscribe to (the timer drives polls), so this is a no-op.
    void initialize() override {}

    /// One pass: walk the manager's backends, build the ViewModel,
    /// notify observers. Cheap (atomic loads + string formatting).
    /// Caller drives cadence -- typically a Glib::signal_timeout
    /// firing every ~500ms.
    void poll();

private:
    integration::IntegrationManager& manager_;
};

}  // namespace app
