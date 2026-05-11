#pragma once

#include "src/integration/IntegrationBackend.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace app::integration::opcua {

/// Abstract OPC-UA client lifecycle + monitored-items surface.
///
/// Mirror of `OpcUaServer` for the inbound direction. Where the server
/// abstracts an OPC-UA endpoint we *expose*, this one abstracts a
/// connection to an OPC-UA endpoint we *consume from* -- typical
/// HMI/SCADA role of reading PLC telemetry over OPC-UA.
///
/// Single interface: a concrete `OpcUaClient` is ALSO an
/// `IntegrationBackend`, so `IntegrationManager` orchestrates it
/// uniformly with TCP / MQTT / our OPC-UA server. Bridges that want
/// to ingest specific nodes downcast via a const reference returned
/// from a backend-specific accessor (composition root wires this).
///
/// SOLID:
///   * S -- one job: connect to a remote OPC-UA server and deliver
///     value-change notifications for nodes the caller subscribed to.
///     Knows nothing about which nodes mean what (a `OpcUaIngestBridge`
///     translates them into Model mutations); knows nothing about
///     publishing back (that's the server side, separate hierarchy).
///   * O -- a new client engine (commercial SDK, hand-rolled stack)
///     is a new subclass; bridges and the backend wrapper stay
///     untouched.
///   * L -- every concrete honours the start/stop/subscribe contract;
///     a `MockOpcUaClient` is substitutable in unit tests.
///   * I -- intentionally narrow: lifecycle + three typed subscribe
///     entry points (Float / Int32 / Bool). String / DateTime live in
///     a follow-up when an actual use case arrives.
///   * D -- ingestion bridges depend on this abstraction, never on
///     the concrete `Open62541Client`.
///
/// Threading: subscribe callbacks fire on the client's I/O thread.
/// Callers that touch shared state must marshal or lock; the
/// `OpcUaIngestBridge` documents this expectation at its layer.
///
/// Rule of 5: copy / move deleted -- a concrete owns a non-copyable
/// `UA_Client*` and a worker thread.
class OpcUaClient : public IntegrationBackend {
public:
    /// Each typed callback receives the slash-separated browse path
    /// (so a single handler can multiplex by topic) and the freshly
    /// notified value.
    using FloatCallback =
        std::function<void(std::string_view nodeBrowsePath, float value)>;
    using Int32Callback =
        std::function<void(std::string_view nodeBrowsePath,
                           std::int32_t value)>;
    using BoolCallback =
        std::function<void(std::string_view nodeBrowsePath, bool value)>;

    OpcUaClient(const OpcUaClient&)            = delete;
    OpcUaClient& operator=(const OpcUaClient&) = delete;
    OpcUaClient(OpcUaClient&&)                 = delete;
    OpcUaClient& operator=(OpcUaClient&&)      = delete;
    ~OpcUaClient() override                    = default;

    /// Register a monitored item for the node identified by
    /// `nodeBrowsePath` (slash-separated under `Objects/`,
    /// e.g. `Factory/EquipmentLines/Line0/Status`). The callback fires
    /// on every value-change notification the server publishes for
    /// that node.
    ///
    /// Returns false if the node doesn't exist, the type doesn't
    /// match, or the subscribe was rejected. The client is responsible
    /// for ensuring the call is safe to make both before and after
    /// `start()` -- pre-start subscriptions are replayed once the
    /// session is up, mirroring `MqttClient::subscribe`.
    [[nodiscard]] virtual bool
        subscribeFloat(std::string_view nodeBrowsePath,
                       FloatCallback callback) = 0;

    [[nodiscard]] virtual bool
        subscribeInt32(std::string_view nodeBrowsePath,
                       Int32Callback callback) = 0;

    [[nodiscard]] virtual bool
        subscribeBool(std::string_view nodeBrowsePath,
                      BoolCallback callback) = 0;

    /// Number of monitored items the client has succesfully armed on
    /// the server. Useful for the I/O panel pill metrics and tests.
    [[nodiscard]] virtual std::size_t monitoredItemCount() const noexcept = 0;

    /// Server URL the client is configured to dial. Stable for the
    /// lifetime of one instance; survives start/stop cycles.
    [[nodiscard]] virtual std::string endpointUrl() const = 0;

protected:
    OpcUaClient() = default;
};

}  // namespace app::integration::opcua
