#pragma once

#include "src/integration/opcua/OpcUaServer.h"

#include <gmock/gmock.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace app::integration::opcua::testing {

/// Recording mock for `OpcUaServer`. Records every state-mutating
/// call so tests can assert on exact paths + values without simulating
/// a real OPC-UA address space.
///
/// Lives in the `testing` sub-namespace so production code can never
/// accidentally include it -- tests opt in explicitly. Keeps gmock
/// out of the production-code dependency surface.
class MockOpcUaServer final : public OpcUaServer {
public:
    MockOpcUaServer()           = default;
    ~MockOpcUaServer() override = default;

    MockOpcUaServer(const MockOpcUaServer&)            = delete;
    MockOpcUaServer& operator=(const MockOpcUaServer&) = delete;
    MockOpcUaServer(MockOpcUaServer&&)                 = delete;
    MockOpcUaServer& operator=(MockOpcUaServer&&)      = delete;

    MOCK_METHOD(void, start, (), (override));
    MOCK_METHOD(void, stop, (), (noexcept, override));
    MOCK_METHOD(bool, isRunning, (), (const, noexcept, override));
    MOCK_METHOD(std::size_t, connectedSessions, (),
                (const, noexcept, override));
    MOCK_METHOD(std::uint16_t, boundPort, (), (const, noexcept, override));

    MOCK_METHOD(bool, writeFloat,
                (std::string_view path, float value), (noexcept, override));
    MOCK_METHOD(bool, writeInt32,
                (std::string_view path, std::int32_t value),
                (noexcept, override));
    MOCK_METHOD(bool, writeBool,
                (std::string_view path, bool value), (noexcept, override));
    MOCK_METHOD(bool, writeString,
                (std::string_view path, std::string_view value),
                (noexcept, override));

    MOCK_METHOD(bool, addObject, (std::string_view path), (override));
    MOCK_METHOD(bool, addFloatVariable,
                (std::string_view path, float initial), (override));
    MOCK_METHOD(bool, addInt32Variable,
                (std::string_view path, std::int32_t initial), (override));
    MOCK_METHOD(bool, addBoolVariable,
                (std::string_view path, bool initial), (override));
    MOCK_METHOD(bool, addStringVariable,
                (std::string_view path, std::string_view initial), (override));
    MOCK_METHOD(bool, addMethod,
                (std::string_view path, OpcUaCommandSink& sink), (override));
    MOCK_METHOD(bool, addBoolVariableWithWriteCallback,
                (std::string_view path, bool initial,
                 OpcUaCommandSink& sink),
                (override));
};

}  // namespace app::integration::opcua::testing
