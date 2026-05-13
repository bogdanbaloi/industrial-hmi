#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace app::integration::modbus {

/// Modbus register class -- determines which function code (FC03 vs
/// FC04) the poll loop will use to read the address. Holding
/// registers are RW (FC03/FC06); input registers are R-only (FC04).
/// The MVP only reads, so both classes go through the same code path
/// distinguished only by the function code.
enum class RegisterType : std::uint8_t {
    HoldingRegister,  ///< FC03 -- RW from a master's perspective
    InputRegister,    ///< FC04 -- R-only
};

/// Domain field a single Modbus register maps to.
enum class FieldKind : std::uint8_t {
    /// Register value 0 -> equipment OFF; any non-zero -> ON.
    /// Matches the convention every Modbus coil-style master uses
    /// (the "enabled" bit comes through as a 16-bit register with
    /// value 0x0000 / 0x0001 on most PLCs).
    EquipmentEnabled,

    /// Equipment supply level, 0..100 percent. The raw register
    /// value is multiplied by `RegisterMapping::scale` then clamped
    /// to the domain by the model. Common PLC conventions:
    ///   scale 1.0    raw value is direct percent (raw 85 -> 85%)
    ///   scale 0.1    16-bit fixed-point (raw 850 -> 85.0%)
    ///   scale 0.001  raw counts -> divided to percent
    EquipmentSupplyLevel,

    /// Quality checkpoint pass rate, 0..100 percent. Same scale
    /// semantics as EquipmentSupplyLevel. The model stores passRate
    /// as float, so the bridge applies the scale in float space
    /// before forwarding.
    QualityPassRate,
};

/// One row of the register map: "slave X register Y means field F of
/// entity Z". Plain aggregate so callers compose maps from JSON
/// config without going through a builder.
struct RegisterMapping {
    std::uint8_t  slaveId{1};
    RegisterType  type{RegisterType::HoldingRegister};
    std::uint16_t address{0};
    FieldKind     field{FieldKind::EquipmentEnabled};

    /// Domain identifier this register pertains to. For
    /// EquipmentEnabled / EquipmentSupplyLevel this is the equipment
    /// slot id (0..N-1); for QualityPassRate it's the checkpoint id.
    std::uint32_t entityId{0};

    /// Linear scale applied to the raw register value before the
    /// bridge forwards it to the model. Defaults to 1.0 (pass-through);
    /// PLCs that ship fixed-point readings (raw 850 means 85.0%) use
    /// 0.1, and so on. Ignored for EquipmentEnabled (boolean fields).
    float scale{1.0f};
};

/// Container for register mappings. Just a thin wrapper over a vector
/// -- the only behaviour worth a class is preserving insertion order
/// (poll loop iterates in registration order so deterministic round-
/// robin against slaves is the operator's, not the container's,
/// concern) and exposing a non-owning `std::span` view for the poll
/// loop to walk without copying.
///
/// SOLID:
///   * S -- one job: own the mapping table and hand out a read-only
///     view of it.
///   * O -- a new FieldKind doesn't change this container at all; it
///     changes the bridge's dispatch.
///   * L -- not virtual; bridges depend on the concrete class. If a
///     second mapper shape ever shows up (rare -- register maps are
///     a stable concept), promote to an interface then.
class ModbusRegisterMap {
public:
    ModbusRegisterMap() = default;

    ModbusRegisterMap(const ModbusRegisterMap&) = default;
    ModbusRegisterMap& operator=(const ModbusRegisterMap&) = default;
    ModbusRegisterMap(ModbusRegisterMap&&) noexcept = default;
    ModbusRegisterMap& operator=(ModbusRegisterMap&&) noexcept = default;
    ~ModbusRegisterMap() = default;

    /// Append a mapping. No duplicate detection -- two entries with
    /// the same address are legal (e.g. mirror a register into two
    /// entities). The poll loop honours both; the bridge dedupes per
    /// (entity, field).
    void add(RegisterMapping mapping) {
        entries_.push_back(mapping);
    }

    /// Read-only view over the mappings. Safe to walk while the map
    /// is otherwise unmodified.
    [[nodiscard]] std::span<const RegisterMapping> entries() const noexcept {
        return {entries_.data(), entries_.size()};
    }

    [[nodiscard]] bool empty() const noexcept {
        return entries_.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

    void clear() noexcept {
        entries_.clear();
    }

private:
    std::vector<RegisterMapping> entries_;
};

}  // namespace app::integration::modbus
