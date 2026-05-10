#pragma once

#include <string>

namespace app::integration {

/// Coarse-grained health classification surfaced to the UI.
///
/// Kept deliberately small: the dashboard renders these as a coloured
/// dot, so adding states should be driven by a UI need, not by every
/// possible internal failure mode (those live in metricsSummary()).
enum class BackendState {
    /// Not started, or stopped cleanly. Default for a fresh backend.
    Disconnected,
    /// `start()` returned, but the channel hasn't reached a usable
    /// state yet (TCP listener not yet bound, MQTT broker handshake
    /// in flight, OPC-UA endpoint advertising).
    Connecting,
    /// Healthy: actually serving / publishing right now.
    Connected,
    /// Running but with errors -- broker dropped, connection reset,
    /// last publish() failed. Backends keep retrying internally; the
    /// dashboard surfaces the degraded badge so operators notice.
    Degraded,
};

/// Long-lived I/O backend that exposes the application over a network
/// protocol (TCP, MQTT, gRPC, ...).
///
/// SOLID:
///   * S -- one job: own the lifecycle of an external-facing channel.
///     No format conversion (delegates to Serializer), no business
///     logic (delegates to ProductionModel / ProductsRepository), no
///     UI (the GTK / Console front-ends never see this layer).
///   * O -- adding a new protocol is a new subclass; existing backends
///     and the IntegrationManager stay untouched.
///   * L -- IntegrationManager treats every backend uniformly through
///     this interface. start() / stop() must respect the same contract
///     across all concretes.
///   * I -- intentionally narrow. No Send / Receive / Subscribe in the
///     base. Each concrete exposes its own protocol-specific surface
///     internally; this interface only covers what the manager needs.
///   * D -- backends depend on `ProductionModel&` / `ProductsRepository&`
///     (interfaces) injected via constructor, never on the SimulatedModel
///     or DatabaseManager singletons.
///
/// Lifecycle contract:
///   * `start()` must be non-blocking. It launches background work
///     (acceptor loop, MQTT reconnect, etc.) and returns. Throws on
///     unrecoverable setup failure (port-in-use, broker unreachable
///     during initial connect).
///   * `stop()` must be blocking. It tears down threads, drains
///     in-flight work, and returns only when the backend is fully
///     idle. Safe to call from a destructor.
///   * `isRunning()` reflects the post-start, pre-stop state. Useful
///     for status reporting and shutdown progress logs.
class IntegrationBackend {
public:
    virtual ~IntegrationBackend() = default;

    IntegrationBackend(const IntegrationBackend&) = delete;
    IntegrationBackend& operator=(const IntegrationBackend&) = delete;
    IntegrationBackend(IntegrationBackend&&) = delete;
    IntegrationBackend& operator=(IntegrationBackend&&) = delete;

    /// Start the backend. Non-blocking; throws on unrecoverable setup
    /// errors (e.g. EADDRINUSE for TCP, broker resolve failure for MQTT).
    virtual void start() = 0;

    /// Stop the backend. Blocking; returns once all background threads
    /// joined and pending I/O drained. Safe to call from destructor or
    /// after a previous start() failure.
    virtual void stop() = 0;

    /// Live state. False after construction (before start) and after
    /// stop(); true between successful start() and stop().
    [[nodiscard]] virtual bool isRunning() const = 0;

    /// Short stable name for log messages and status displays
    /// (e.g. "TCP", "MQTT"). Not localised.
    [[nodiscard]] virtual std::string name() const = 0;

    /// Coarse-grained health classification for the dashboard
    /// backend-health bar. Default impl returns `Connected` whenever
    /// `isRunning()` is true and `Disconnected` otherwise -- enough for
    /// backends that have no degraded mode to model. Concretes that
    /// can detect connecting / degraded states (MQTT during handshake,
    /// TCP after a publish failure) override.
    [[nodiscard]] virtual BackendState connectionState() const noexcept {
        return isRunning() ? BackendState::Connected
                           : BackendState::Disconnected;
    }

    /// Single-line human-readable metrics for the tooltip on the
    /// dashboard health badge. Examples: "2 clients", "broker
    /// 192.168.1.10:1883", "1 OPC-UA session". Default returns the
    /// empty string -- backends that have nothing meaningful to
    /// report leave it blank and the UI hides the tooltip.
    [[nodiscard]] virtual std::string metricsSummary() const {
        return {};
    }

protected:
    IntegrationBackend() = default;
};

}  // namespace app::integration
