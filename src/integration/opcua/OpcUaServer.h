#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace app::integration::opcua {

/// Abstract OPC-UA server lifecycle and node-write surface.
///
/// SOLID:
///   * S -- one job: own the wire-level OPC-UA endpoint. Knows nothing
///     about which nodes exist (that's `OpcUaNodeMap`) or what to do
///     when a client invokes a method (that's `OpcUaCommandSink`).
///   * O -- a new server engine (e.g. a hand-rolled OPC-UA stack, or
///     the commercial `prosys-opcua-sdk-cpp`) is a new subclass without
///     touching `OpcUaBackend` or any node map.
///   * L -- every concrete must respect the start/stop contract below.
///     `OpcUaBackend` is written against this interface only and cannot
///     tell the difference between a real server and a `MockOpcUaServer`
///     used in unit tests.
///   * I -- intentionally small. Browse, history, encryption, alarms
///     are out of scope; this surface only covers what the node map
///     and command sink need to publish telemetry and accept method
///     calls. Extension surfaces (e.g. encryption) get their own
///     orthogonal interface, not new methods here.
///   * D -- node maps and the backend depend on this interface, never
///     on the concrete `Open62541Server`.
///
/// Lifecycle contract:
///   * `start()` is non-blocking. It binds the listening socket, spins
///     up the I/O thread, and returns once the endpoint is actually
///     accepting connections. Throws `std::runtime_error` on bind /
///     resource failure so callers can surface a typed error rather
///     than a silent half-started server.
///   * `stop()` is blocking. It signals the I/O thread, joins it, and
///     returns only when the endpoint no longer listens. Idempotent --
///     calling stop() twice or stop() before start() is a no-op.
///   * `isRunning()` reflects the post-start, pre-stop state. Cheap
///     enough to call from a status-poll timer.
///
/// Rule of 5: copy / move are deleted because every concrete owns a
/// non-copyable native handle (e.g. `UA_Server*`). Movability is not
/// useful at the interface level -- backends hold these via
/// `std::unique_ptr` and never swap ownership.
class OpcUaServer {
public:
    virtual ~OpcUaServer() = default;

    OpcUaServer(const OpcUaServer&)            = delete;
    OpcUaServer& operator=(const OpcUaServer&) = delete;
    OpcUaServer(OpcUaServer&&)                 = delete;
    OpcUaServer& operator=(OpcUaServer&&)      = delete;

    /// Bind the endpoint and start serving. Throws on bind failure.
    /// Post-condition: `isRunning() == true` on return.
    virtual void start() = 0;

    /// Stop serving and join the I/O thread. Idempotent; safe from
    /// destructors. Post-condition: `isRunning() == false` on return.
    virtual void stop() noexcept = 0;

    [[nodiscard]] virtual bool isRunning() const noexcept = 0;

    /// Live count of connected OPC-UA sessions. Used by the dashboard
    /// backend-health badge. Returns 0 when not running.
    [[nodiscard]] virtual std::size_t connectedSessions() const noexcept = 0;

    /// Effective port the server bound to. Differs from the configured
    /// port only when configured port was 0 (ephemeral). Stable for the
    /// lifetime of a single start/stop cycle. Returns 0 when not
    /// running.
    [[nodiscard]] virtual std::uint16_t boundPort() const noexcept = 0;

    /// Write a Float scalar to the node identified by `nodeBrowsePath`
    /// (slash-separated browse path under `Objects/`, e.g.
    /// `Factory/Line0/Throughput`). Returns false if the node does not
    /// exist or the write was rejected by the server. Non-throwing so
    /// telemetry update paths stay nothrow-able.
    [[nodiscard]] virtual bool
        writeFloat(std::string_view nodeBrowsePath, float value) noexcept = 0;

    [[nodiscard]] virtual bool
        writeInt32(std::string_view nodeBrowsePath, std::int32_t value) noexcept = 0;

    [[nodiscard]] virtual bool
        writeBool(std::string_view nodeBrowsePath, bool value) noexcept = 0;

    [[nodiscard]] virtual bool
        writeString(std::string_view nodeBrowsePath,
                    std::string_view value) noexcept = 0;

protected:
    OpcUaServer() = default;
};

}  // namespace app::integration::opcua
