#pragma once

#include "src/integration/opcua/OpcUaCommandSink.h"

#include <string_view>

namespace app::core  { class Logger; }
namespace app::model { class ProductionModel; }

namespace app::integration::opcua {

/// Concrete `OpcUaCommandSink` that turns inbound OPC-UA method
/// invocations and writable-variable updates into mutations on a
/// `ProductionModel`.
///
/// Domain mapping:
///
///   onCommand("StartProduction")   -> model.startProduction()
///   onCommand("StopProduction")    -> model.stopProduction()
///   onCommand("ResetSystem")       -> model.resetSystem()
///   onCommand("StartCalibration")  -> model.startCalibration()
///
///   onBoolWrite("Factory/EquipmentLines/Line<id>/Enabled", v)
///                                  -> model.setEquipmentEnabled(id, v)
///
/// Anything else is logged as a warning and dropped -- a misbehaving
/// or out-of-date client must not be able to crash the HMI by
/// invoking a fictional method or writing to an unknown path.
///
/// SOLID:
///   * S -- one job: translate inbound OPC-UA names + payloads into
///     model calls. No socket, no UI, no logging policy beyond the
///     drop-with-warning contract.
///   * D -- depends on `ProductionModel&` and `core::Logger&`
///     abstractions; the concrete model implementation (Simulator vs.
///     real PLC bridge) is selected at composition root.
///
/// Threading: invoked from the OPC-UA server's I/O thread. Model
/// setters document their own thread safety; the sink doesn't
/// marshal further.
class FactoryCommandSink final : public OpcUaCommandSink {
public:
    FactoryCommandSink(model::ProductionModel& model, core::Logger& logger);

    ~FactoryCommandSink() override = default;

    // OpcUaCommandSink
    void onCommand(std::string_view commandName) noexcept override;
    void onBoolWrite(std::string_view nodePath,
                     bool value) noexcept override;

private:
    /// Parse `Factory/EquipmentLines/Line<id>/Enabled` and call back
    /// into `model_.setEquipmentEnabled`. Returns false if the path
    /// doesn't match the expected shape so `onBoolWrite` can fall
    /// through to a warning.
    [[nodiscard]] bool dispatchEquipmentEnabled(std::string_view nodePath,
                                                bool value);

    model::ProductionModel& model_;
    core::Logger&           logger_;
};

}  // namespace app::integration::opcua
