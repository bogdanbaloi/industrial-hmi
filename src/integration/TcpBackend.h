#pragma once

#include "src/integration/IntegrationBackend.h"
#include "src/model/ProductionModel.h"
#include "src/model/ProductsRepository.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

// Forward-declare Asio types so the header doesn't pull <boost/asio.hpp>
// into every translation unit that includes us. Foreign API naming --
// we don't get to rename Boost's io_context to CamelCase.
namespace boost::asio {
    // NOLINTNEXTLINE(readability-identifier-naming)
    class io_context;
}

namespace app::integration {

/// TCP backend that exposes the application over a line-oriented text
/// protocol. Mirrors the headless console binary's command set so the
/// same shell script can drive either:
///
///   $ printf 'status\nquit\n' | ./industrial-hmi-console
///   $ printf 'status\nquit\n' | nc localhost 5555
///
/// Wire protocol (newline-terminated, ASCII):
///   > products              -> count line, then N JSON object lines
///   > status                -> single JSON line (snapshot)
///   > eq <id> on|off        -> "OK" or "ERR <reason>"
///   > production start      -> "OK"
///   > production stop       -> "OK"
///   > production reset      -> "OK"
///   > help                  -> usage banner
///   > quit                  -> "BYE", server closes the connection
///
/// One client per accept loop iteration; commands are handled
/// sequentially within each connection. Multiple concurrent clients
/// share the io_context but are isolated -- a slow / dead client
/// can't block the others past the read timeout.
///
/// SOLID / threading:
///   * Owns its own `boost::asio::io_context` + `std::jthread`.
///     Isolated from ModelContext's I/O thread so a stuck TCP client
///     can't starve async DB writes (or vice versa).
///   * Depends on `ProductionModel&` and `ProductsRepository&`
///     interfaces (NOT SimulatedModel / DatabaseManager singletons)
///     so tests inject mocks for hermetic verification.
///   * The injected references must outlive this backend. Typical
///     wiring keeps them in main.cpp scope alongside Bootstrap.
class TcpBackend : public IntegrationBackend {
public:
    /// @param port           TCP port to bind. 0 == auto-assign by OS
    ///                       (useful in tests; query via `boundPort()`).
    /// @param production     Production model (DI -- usually
    ///                       SimulatedModel singleton in production,
    ///                       MockProductionModel in tests).
    /// @param products       Read-side products repository (DI -- usually
    ///                       DatabaseManager singleton, mock in tests).
    TcpBackend(std::uint16_t port,
               model::ProductionModel& production,
               model::ProductsRepository& products);

    ~TcpBackend() override;

    void start() override;
    void stop() override;

    [[nodiscard]] bool isRunning() const override {
        return running_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::string name() const override { return "TCP"; }

    /// Actual port the acceptor bound to. Equals the constructor's
    /// `port` for non-zero values; for `port == 0` returns whatever the
    /// OS picked. Returns 0 before start() succeeds.
    [[nodiscard]] std::uint16_t boundPort() const noexcept {
        return boundPort_.load(std::memory_order_acquire);
    }

private:
    /// Non-virtual stop() body. Called from both the public virtual
    /// stop() and the destructor; the destructor cannot call a virtual
    /// safely (clang-analyzer-optin.cplusplus.VirtualCall).
    void stopImpl() noexcept;

    /// Body of the io_context's worker thread.
    void runIoLoop();

    /// Per-connection synchronous command dispatcher. Returns when
    /// the client sends "quit" or closes the socket. The socket is
    /// moved in -- the caller must not use it afterwards.
    /// Pimpl-friendly: declared with `void*` because the header
    /// can't include <boost/asio.hpp> (would force every TU that
    /// pulls TcpBackend.h to include the entire Asio header).
    /// The .cpp does the actual cast.
    void handleConnection(void* tcpSocketPtr);

    /// Dispatch a single text command line. Writes its response (one
    /// or more lines, each newline-terminated) to `out` and returns
    /// true to keep the connection open or false to close it (after
    /// sending "BYE").
    bool dispatchCommand(const std::string& line, std::string& out);

    std::uint16_t requestedPort_;
    std::atomic<std::uint16_t> boundPort_{0};

    model::ProductionModel& production_;
    model::ProductsRepository& products_;

    std::unique_ptr<boost::asio::io_context> io_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace app::integration
