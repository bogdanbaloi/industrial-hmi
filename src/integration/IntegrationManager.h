#pragma once

#include "src/integration/IntegrationBackend.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace app::integration {

/// Composition root for IntegrationBackend instances.
///
/// Owns a collection of backends and fans out the start / stop signals
/// to every member uniformly. Per Liskov, the manager treats TCP, MQTT,
/// and any future backend exactly the same way -- it never downcasts.
///
/// Typical wiring lives in `main.cpp` after Bootstrap completes:
/// register backends configured via JSON (TCP if `network.tcp.enabled`,
/// MQTT if `network.mqtt.broker` non-empty), call `startAll()` once,
/// `stopAll()` from the shutdown path.
///
/// Failure during `startAll()` does not abort the remaining backends:
/// each one's exception is caught, logged via the supplied logger
/// (when set), and added to a list the caller can inspect via
/// `lastStartErrors()`. This mirrors the production HMI policy that a
/// degraded but partially-functional binary is preferable to a hard
/// abort -- if MQTT can't reach its broker, TCP should still be up.
class IntegrationManager {
public:
    IntegrationManager() = default;
    ~IntegrationManager();

    IntegrationManager(const IntegrationManager&) = delete;
    IntegrationManager& operator=(const IntegrationManager&) = delete;
    IntegrationManager(IntegrationManager&&) = delete;
    IntegrationManager& operator=(IntegrationManager&&) = delete;

    /// Adopts ownership. Does NOT call start() -- callers compose first,
    /// then start the whole bundle once via `startAll()`.
    void registerBackend(std::unique_ptr<IntegrationBackend> backend);

    /// How many backends have been registered.
    [[nodiscard]] std::size_t backendCount() const noexcept {
        return backends_.size();
    }

    /// Start every registered backend. Exceptions from individual
    /// start() calls are caught and recorded in `lastStartErrors()`;
    /// remaining backends still get their chance.
    void startAll();

    /// Stop every running backend. Idempotent and noexcept-by-contract
    /// (concrete backends are expected to swallow stop-time errors).
    /// Safe to call from a destructor.
    void stopAll() noexcept;

    /// True iff every registered backend reports isRunning() == true.
    /// False if any backend is down or no backends were registered.
    [[nodiscard]] bool allRunning() const noexcept;

    /// Diagnostic accessor for the most recent batch of start() failures.
    /// Empty when the last `startAll()` succeeded for every backend.
    /// Format: "<backend-name>: <exception-what>" per entry.
    [[nodiscard]] const std::vector<std::string>&
        lastStartErrors() const noexcept {
        return lastStartErrors_;
    }

private:
    std::vector<std::unique_ptr<IntegrationBackend>> backends_;
    std::vector<std::string> lastStartErrors_;
};

}  // namespace app::integration
