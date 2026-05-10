#include "src/presenter/BackendHealthPresenter.h"

#include "src/integration/IntegrationManager.h"
#include "src/presenter/modelview/BackendHealthViewModel.h"

namespace app {

BackendHealthPresenter::BackendHealthPresenter(
    integration::IntegrationManager& manager)
    : manager_(manager) {}

void BackendHealthPresenter::poll() {
    presenter::BackendHealthViewModel vm;
    const auto& backends = manager_.backends();
    vm.entries.reserve(backends.size());

    for (const auto& backend : backends) {
        if (!backend) continue;
        presenter::BackendHealthViewModel::Entry entry;
        entry.name = backend->name();
        entry.state = backend->connectionState();
        entry.metricsLine = backend->metricsSummary();
        vm.entries.push_back(std::move(entry));
    }

    notifyAll(&ViewObserver::onBackendHealthChanged, vm);
}

}  // namespace app
