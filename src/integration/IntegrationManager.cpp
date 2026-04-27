#include "src/integration/IntegrationManager.h"

#include <exception>
#include <utility>

namespace app::integration {

IntegrationManager::~IntegrationManager() {
    // Defensive cleanup if the caller forgot to stopAll() explicitly.
    // Backends are required to make stop() safe-on-already-stopped.
    stopAll();
}

void IntegrationManager::registerBackend(
        std::unique_ptr<IntegrationBackend> backend) {
    if (backend) {
        backends_.push_back(std::move(backend));
    }
}

void IntegrationManager::startAll() {
    lastStartErrors_.clear();
    for (auto& backend : backends_) {
        if (!backend) continue;
        try {
            backend->start();
        } catch (const std::exception& e) {
            // Record + continue. One backend's failure must not prevent
            // the rest from coming up.
            lastStartErrors_.push_back(
                backend->name() + ": " + e.what());
        }
    }
}

void IntegrationManager::stopAll() noexcept {
    for (auto& backend : backends_) {
        if (!backend) continue;
        // Concrete backends own their stop()-time error handling;
        // we silence anything that escapes so shutdown remains
        // deterministic. The catch BODY is intentionally a no-op --
        // logger isn't available during late shutdown and there's
        // nowhere meaningful to surface a stop-time failure.
        // NOLINTNEXTLINE(bugprone-empty-catch)
        try { backend->stop(); } catch (...) { /* swallow */ }
    }
}

bool IntegrationManager::allRunning() const noexcept {
    if (backends_.empty()) return false;
    for (const auto& backend : backends_) {
        if (!backend || !backend->isRunning()) return false;
    }
    return true;
}

}  // namespace app::integration
