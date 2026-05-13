#pragma once

#include "src/integration/modbus/ModbusRegisterMap.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace app::model { class ProductionModel; }

namespace app::integration::modbus {

/// Inbound bridge: turns raw 16-bit register values (from the poll
/// loop) into ProductionModel setter calls.
///
/// Mirror of `SensorIngestBridge` (MQTT side) and
/// `OpcUaIngestBridge` -- same SoC split between transport and
/// domain, same constraint (the only inbound setter the model
/// exposes today is `setEquipmentEnabled`, so we only translate to
/// that one field).
///
/// SOLID:
///   * S -- one job: dispatch one (mapping, raw value) pair to the
///     right model setter. No socket, no cadence, no decoding.
///   * O -- a new FieldKind = a new case in `onRegisterChanged`'s
///     switch; the poll loop and the register map don't change.
///   * L -- depends on `ProductionModel&` abstraction, never on
///     SimulatedModel.
///   * I -- intentionally narrow: one public method (plus ctor /
///     dtor). The poll loop is the only caller.
///   * D -- model mutation goes through the abstract base.
///
/// Threading: `onRegisterChanged` is invoked by the poll loop on its
/// own jthread. `ProductionModel`'s setters document their thread
/// safety and the bridge respects that by being side-effect-only
/// (no shared mutable state outside the dedup cache below).
///
/// Dedup: the poll loop sends EVERY successful read through here,
/// not just changes -- the bridge owns the "did the semantic value
/// actually flip?" check so the poll loop stays a simple transport
/// driver. For EquipmentEnabled, "flipped" means the boolean derived
/// from the raw value (rawValue != 0) differs from the last value
/// we propagated. First notification for a given entity always
/// passes through.
class ModbusIngestBridge {
public:
    explicit ModbusIngestBridge(model::ProductionModel& model);

    ModbusIngestBridge(const ModbusIngestBridge&)            = delete;
    ModbusIngestBridge& operator=(const ModbusIngestBridge&) = delete;
    ModbusIngestBridge(ModbusIngestBridge&&)                 = delete;
    ModbusIngestBridge& operator=(ModbusIngestBridge&&)      = delete;
    ~ModbusIngestBridge()                                    = default;

    /// Apply one register reading. Safe to call on the poll loop
    /// thread; the model's setter is the synchronisation barrier.
    void onRegisterChanged(const RegisterMapping& mapping,
                           std::uint16_t rawValue);

private:
    model::ProductionModel& model_;

    /// Per-equipment "last enabled bit we propagated". `nullopt` =
    /// no previous observation, so the first reading always fires.
    /// Capped to keep the array on the stack; matches the existing
    /// OpcUaIngestBridge cap.
    static constexpr std::size_t kMaxTrackedEquipment = 16;
    std::array<std::optional<bool>, kMaxTrackedEquipment> lastEnabled_{};
};

}  // namespace app::integration::modbus
