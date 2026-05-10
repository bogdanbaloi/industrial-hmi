#pragma once

#include "src/integration/IntegrationBackend.h"

#include <string>
#include <vector>

namespace app::presenter {

/// Display-ready snapshot of every registered IntegrationBackend.
///
/// Built by `BackendHealthPresenter` and pushed to the View layer.
/// Pure data, no GTK, no model references -- the same struct flows
/// to a hypothetical web dashboard or terminal status page without
/// changing.
struct BackendHealthViewModel {
    struct Entry {
        /// Short stable identifier from `IntegrationBackend::name()`.
        /// Examples: "TCP", "MQTT", "OPC-UA". Used as the dot label.
        std::string name;

        /// Coarse-grained health classification. Drives the CSS class
        /// on the matching dot widget.
        integration::BackendState state{integration::BackendState::Disconnected};

        /// One-line metrics for the tooltip ("port 4840 | 2 sessions").
        /// Empty when the backend has nothing meaningful to report.
        std::string metricsLine;
    };

    std::vector<Entry> entries;
};

}  // namespace app::presenter
