#pragma once

#include "src/core/Result.h"
#include "src/integration/modbus/ModbusPdu.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace app::integration::modbus {

/// Abstraction over "thing that can read Modbus registers".
///
/// Carved out of ModbusClient so the poll loop and any future
/// orchestrator can depend on an interface, not a concrete TCP
/// transport. The concrete `ModbusClient` (Boost.Asio TCP) is the
/// production implementation; tests substitute a FakeModbusReader
/// without spinning up a real socket.
///
/// Mirror of OpcUaClient (interface) / Open62541Client (concrete) on
/// the OPC-UA side: same DIP shape, same naming convention.
///
/// SOLID:
///   * S -- one job: expose two read methods + connection state. No
///     cadence, no register map, no dispatch.
///   * L -- substitutability is the entire point: ModbusClient OR
///     FakeModbusReader, transparently.
///   * I -- narrow on purpose. The future FC06 write path will land
///     as a separate ModbusWriter interface so a reader-only client
///     doesn't have to stub a write it never serves.
///   * D -- ModbusPollLoop depends on this; never on ModbusClient.
///
/// Error type: ModbusClient::IoError is the canonical surface. Re-
/// using its enum keeps the failure model unified across transports.
///
/// Threading: callers serialise their own access. The concrete
/// ModbusClient mutex-guards its calls so a shared reader across
/// threads is safe even though the expected use is single-threaded
/// (one poll loop -> one reader).
class ModbusReader {
public:
    /// Shared IoError surface across all readers. Concrete classes
    /// reuse these codes verbatim.
    enum class IoError : std::uint8_t {
        ConnectionFailed,
        Timeout,
        WriteFailed,
        ReadFailed,
        Disconnected,
        DecodeFailed,
        ServerException,
        InvalidQuantity,
    };

    virtual ~ModbusReader() = default;

    ModbusReader(const ModbusReader&) = delete;
    ModbusReader& operator=(const ModbusReader&) = delete;
    ModbusReader(ModbusReader&&) = delete;
    ModbusReader& operator=(ModbusReader&&) = delete;

    /// FC03 -- read holding registers (RW class).
    [[nodiscard]] virtual app::core::Result<std::vector<std::uint16_t>,
                                            IoError>
    readHoldingRegisters(std::uint8_t unitId,
                         std::uint16_t address,
                         std::uint16_t quantity) = 0;

    /// FC04 -- read input registers (R-only class).
    [[nodiscard]] virtual app::core::Result<std::vector<std::uint16_t>,
                                            IoError>
    readInputRegisters(std::uint8_t unitId,
                       std::uint16_t address,
                       std::uint16_t quantity) = 0;

    /// True iff the underlying transport is currently connected.
    /// Drives the I/O panel pill colour for the Modbus backend.
    [[nodiscard]] virtual bool isConnected() const noexcept = 0;

    /// Last server-exception code observed (cleared on success).
    /// Powers the I/O panel tooltip so an operator can distinguish
    /// "remote rejected" from "wire broken".
    [[nodiscard]] virtual std::optional<ExceptionCode>
    lastExceptionCode() const noexcept = 0;

protected:
    ModbusReader() = default;
};

}  // namespace app::integration::modbus
