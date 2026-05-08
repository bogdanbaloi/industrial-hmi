#pragma once

#include <string_view>

namespace app::integration::opcua {

/// Inbound-command target. Dispatches method calls received from
/// connected OPC-UA clients to the application's command layer
/// (presenters / production model writes).
///
/// SOLID:
///   * S -- one job: route a named command from wire format to the
///     domain layer. Decoding the call (argument unmarshalling) is
///     the server's responsibility; semantic interpretation is the
///     sink's. The split keeps protocol details out of the domain.
///   * I -- minimal surface. We don't need return values here:
///     synchronous failures are handled by throwing into the server
///     stack (which translates to OPC-UA StatusBad_*); asynchronous
///     command results flow back as new node telemetry, which is
///     already covered by `OpcUaServer::writeXxx`.
///   * D -- the server depends on this interface; the production-code
///     binding is `PresenterCommandSink` (or similar) that forwards to
///     the Presenter layer. Tests inject a recording mock.
///
/// Threading: implementations must assume `onCommand` is invoked from
/// the OPC-UA server's I/O thread, not the GTK main loop. Marshaling
/// to the UI thread is the implementer's responsibility (the existing
/// `Glib::signal_idle` pattern in DatabaseManager is the reference).
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
    /// "ResetSystem"). Implementations route to the appropriate
    /// presenter method.
    ///
    /// Must not throw -- any error is logged inside the implementation.
    /// The OPC-UA server already converts thrown exceptions to a
    /// generic StatusBad_InternalError, which is less informative
    /// than a logged trace plus a typed StatusGood/StatusBadXxx
    /// pre-arranged at the server layer.
    virtual void onCommand(std::string_view commandName) noexcept = 0;

protected:
    OpcUaCommandSink() = default;
};

}  // namespace app::integration::opcua
