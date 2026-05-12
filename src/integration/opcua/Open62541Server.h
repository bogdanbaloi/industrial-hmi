#pragma once

#include "src/integration/opcua/OpcUaConfig.h"
#include "src/integration/opcua/OpcUaServer.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace app::core { class Logger; }

namespace app::integration::opcua {

/// open62541-backed `OpcUaServer` implementation.
///
/// The only translation unit in the project that touches the open62541
/// C API; the rest of the codebase only sees the `OpcUaServer`
/// interface. Same isolation pattern as `Open62541` from the C library
/// staying behind the `OpcUaServer` abstraction (and the same isolation
/// strategy used for ONNX Runtime via `OnnxImageClassifier`).
///
/// Threading model:
///   * The constructor and `start()` / `stop()` run on the caller's
///     thread (typically main / IntegrationManager).
///   * After `start()`, an internal `std::jthread` runs
///     `UA_Server_run_iterate()` in a loop until `stop()` flips the
///     atomic guard.
///   * `writeXxx` is called from arbitrary threads (model callbacks).
///     open62541 documents `UA_Server_writeValue` as MT-safe under the
///     default config (`UA_ServerConfig_setDefault` enables an internal
///     mutex around the address-space).
///
/// SOLID:
///   * S -- adapter only. Lifecycle + node-write translation; no
///     domain logic, no node-tree creation (that's `OpcUaNodeMap`).
///   * O -- swapping to a different stack (e.g. open65541 future
///     v2.x, or a commercial SDK) is a new sibling class implementing
///     `OpcUaServer`.
///   * L -- inherits the contract documented in `OpcUaServer.h`.
///   * I -- exposes nothing beyond the base interface.
///   * D -- depends on the C `UA_Server*` opaquely through a pimpl-
///     style `Impl` so the header stays open62541-free; only the .cpp
///     pulls `<open62541/server.h>`.
///
/// Rule of 5: copy / move deleted (owns a non-copyable `UA_Server*` +
/// thread). Destructor does best-effort `stop()` so RAII teardown
/// works even when callers forget.
class Open62541Server final : public OpcUaServer {
public:
    /// @param config Listen port, application URI, etc.
    /// @param logger Used for "started on port N", connection counts,
    ///               run-loop errors. Must outlive this server.
    Open62541Server(OpcUaConfig config, core::Logger& logger);

    ~Open62541Server() override;

    Open62541Server(const Open62541Server&)            = delete;
    Open62541Server& operator=(const Open62541Server&) = delete;
    Open62541Server(Open62541Server&&)                 = delete;
    Open62541Server& operator=(Open62541Server&&)      = delete;

    void start() override;
    void stop() noexcept override;

    [[nodiscard]] bool isRunning() const noexcept override;
    [[nodiscard]] std::size_t connectedSessions() const noexcept override;
    [[nodiscard]] std::uint16_t boundPort() const noexcept override;

    [[nodiscard]] bool
        writeFloat(std::string_view nodeBrowsePath,
                   float value) noexcept override;
    [[nodiscard]] bool
        writeInt32(std::string_view nodeBrowsePath,
                   std::int32_t value) noexcept override;
    [[nodiscard]] bool
        writeBool(std::string_view nodeBrowsePath,
                  bool value) noexcept override;
    [[nodiscard]] bool
        writeString(std::string_view nodeBrowsePath,
                    std::string_view value) noexcept override;

    [[nodiscard]] bool
        addObject(std::string_view browsePath) override;
    [[nodiscard]] bool
        addFloatVariable(std::string_view browsePath, float initial) override;
    [[nodiscard]] bool
        addInt32Variable(std::string_view browsePath, std::int32_t initial) override;
    [[nodiscard]] bool
        addBoolVariable(std::string_view browsePath, bool initial) override;
    [[nodiscard]] bool
        addStringVariable(std::string_view browsePath,
                          std::string_view initial) override;
    [[nodiscard]] bool
        addMethod(std::string_view browsePath,
                  OpcUaCommandSink& sink) override;
    [[nodiscard]] bool
        addBoolVariableWithWriteCallback(std::string_view browsePath,
                                          bool initial,
                                          OpcUaCommandSink& sink) override;

private:
    /// Pimpl holder so the header stays open62541-free. Real definition
    /// in the .cpp where `<open62541/...>` is included.
    struct Impl;

    void runIterateLoop() noexcept;

    OpcUaConfig config_;
    core::Logger& logger_;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
    std::jthread thread_;
};

}  // namespace app::integration::opcua
