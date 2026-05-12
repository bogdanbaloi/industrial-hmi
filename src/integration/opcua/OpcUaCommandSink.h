#pragma once

#include <string_view>

namespace app::integration::opcua {

/// Inbound-command target. Dispatches method calls AND variable
/// writes received from connected OPC-UA clients to the application's
/// command layer (presenters / production model writes).
///
/// SOLID:
///   * S -- one job: route an inbound OPC-UA event from wire format
///     to the domain layer. Decoding the call (argument unmarshalling)
///     is the server's responsibility; semantic interpretation is the
///     sink's. The split keeps protocol details out of the domain.
///   * I -- two methods covering the two inbound OPC-UA surfaces we
///     expose to clients: typed method calls (no args) and writes to
///     writable Boolean variables. Variables of other types would
///     get their own overload here when needed; we deliberately
///     ship only Bool because the only stateful inbound surface
///     the model exposes today is `setEquipmentEnabled(id, bool)`.
///   * D -- the server depends on this interface; the production-code
///     binding is `FactoryCommandSink` that forwards to the Model.
///     Tests inject a recording mock.
///
/// Threading: implementations must assume both methods are invoked
/// from the OPC-UA server's I/O thread, not the GTK main loop.
/// Marshaling to the UI thread is the implementer's responsibility
/// (the existing `Glib::signal_idle` pattern in DatabaseManager is
/// the reference).
///
/// Rule of 5: copy / move deleted; sinks are owned by composition,
/// passed by reference to the server, and never relocated.
class OpcUaCommandSink {
public:
    virtual ~OpcUaCommandSink() = default;

    OpcUaCommandSink(const OpcUaCommandSink&)            = delete;
    OpcUaCommandSink& operator=(const OpcUaCommandSink&) = delete;
    OpcUaCommandSink(OpcUaCommandSink&&)                 = delete;
    OpcUaCommandSink& operator=(OpcUaCommandSink&&)      = delete;

    /// Dispatch a parameter-less command identified by its OPC-UA
    /// browse name (e.g. "StartProduction", "StopProduction",
    /// "ResetSystem", "StartCalibration"). Implementations route to
    /// the appropriate model method.
    ///
    /// Must not throw -- any error is logged inside the implementation.
    /// The OPC-UA server already converts thrown exceptions to a
    /// generic StatusBad_InternalError, which is less informative
    /// than a logged trace plus a typed StatusGood/StatusBadXxx
    /// pre-arranged at the server layer.
    virtual void onCommand(std::string_view commandName) noexcept = 0;

    /// Dispatch a write to a writable Boolean variable. `nodePath` is
    /// the slash-separated browse path under `Objects/`, e.g.
    /// `Factory/EquipmentLines/Line0/Enabled`. Implementations map
    /// it to the corresponding model setter.
    ///
    /// Same noexcept contract as `onCommand`.
    virtual void onBoolWrite(std::string_view nodePath,
                             bool value) noexcept = 0;

protected:
    OpcUaCommandSink() = default;
};

}  // namespace app::integration::opcua
