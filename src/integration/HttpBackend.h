#pragma once

#include "src/integration/HttpTlsMaterial.h"
#include "src/integration/HttpTlsOptions.h"
#include "src/integration/IntegrationBackend.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Forward-declare cpp-httplib's server so the (large, header-only)
// <httplib.h> stays out of every translation unit that includes us; only
// HttpBackend.cpp pulls it in. Foreign API naming -- we don't get to rename
// httplib::Server to our convention.
namespace httplib {
// NOLINTNEXTLINE(readability-identifier-naming)
class Server;
}

namespace app::model {
class ProductionModel;
class ProductsRepository;
}
namespace app::presenter {
class AlertCenter;
}
namespace app::core {
class Logger;
}

namespace app::integration {

/// Read-only REST/HTTP backend: the standard-web-client sibling of the
/// TCP / MQTT / Modbus / OPC-UA protocols, serving the same model /
/// products / alarm seams over five `GET` routes (ADR-0025,
/// REQ-INTEGRATION-007, REQ-INTEGRATION-008):
///
///   GET /health     -> {"status":"ok"}                  (no model access)
///   GET /status     -> buildStatusJson(ProductionModel) (shared with TCP)
///   GET /alarms     -> app::mcp::runAlarmsSnapshot(AlertCenter) (shared MCP)
///   GET /products   -> ProductsRepository::getAllProducts() as a JSON array
///   GET /production -> buildProductionMetricsJson(ProductionModel) (shared MCP)
///
/// No state-changing route is exposed; a write endpoint waits on the same
/// authorization story the MCP write tool was deferred behind (ADR-0023).
///
/// SOLID / threading:
///   * Owns a cpp-httplib `Server` + a `std::jthread` running its accept
///     loop, isolated from the model's I/O threads (same posture as
///     TcpBackend). `start()` binds synchronously (fails fast on a busy
///     port) then spins the loop; `stop()` calls `Server::stop()` to
///     unblock the loop before joining.
///   * Depends on `ProductionModel&`, `ProductsRepository&` and
///     `presenter::AlertCenter&` interfaces (never the singletons) so tests
///     inject fakes. The injected references + logger must outlive the
///     backend.
///
/// TLS (REQ-INTEGRATION-010, ADR-0030): passing `HttpTlsOptions` switches the
/// accept loop to `httplib::SSLServer`, server-only or mutual-auth. The cert
/// / key material is proven in THIS constructor, so a deployment that
/// configured TLS and cannot have it fails at startup instead of quietly
/// serving plaintext on a port the operator believes is encrypted.
class HttpBackend final : public IntegrationBackend {
public:
    /// @param port          TCP port to bind. 0 == auto-assign by the OS
    ///                      (useful in tests; read back via `boundPort()`).
    /// @param bindAddress   Interface to bind (e.g. "127.0.0.1").
    /// @param production    Production model (DI).
    /// @param products      Read-side products repository (DI).
    /// @param alerts        Alarm store the `/alarms` route projects (DI).
    /// @param logger        Start/stop traces + handler-exception (500)
    ///                      logging. Must outlive this backend.
    /// @param tlsOptions    Absent (the default) serves plaintext HTTP.
    ///                      Present switches the server to TLS. The cert /
    ///                      key (and client CA when `verifyPeer` is set) are
    ///                      loaded and verified right here.
    /// @throws core::TlsMaterialError when `tlsOptions` is present and the
    ///         configured material cannot be loaded. There is deliberately
    ///         no plaintext fallback.
    HttpBackend(std::uint16_t port,
                std::string bindAddress,
                model::ProductionModel& production,
                model::ProductsRepository& products,
                presenter::AlertCenter& alerts,
                core::Logger& logger,
                std::optional<HttpTlsOptions> tlsOptions = std::nullopt);

    ~HttpBackend() override;

    HttpBackend(const HttpBackend&)            = delete;
    HttpBackend& operator=(const HttpBackend&) = delete;
    HttpBackend(HttpBackend&&)                 = delete;
    HttpBackend& operator=(HttpBackend&&)      = delete;

    void start() override;
    void stop() override;

    [[nodiscard]] bool isRunning() const override {
        return running_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::string name() const override { return "HTTP"; }
    [[nodiscard]] std::string metricsSummary() const override;

    /// Actual port the server bound to. Equals the constructor's `port`
    /// for non-zero values; for `port == 0` returns whatever the OS
    /// picked. Returns 0 before start() succeeds.
    [[nodiscard]] std::uint16_t boundPort() const noexcept {
        return boundPort_.load(std::memory_order_acquire);
    }

    /// True when this backend serves HTTPS (REQ-INTEGRATION-010). Fixed at
    /// construction: TLS is never turned on or off by a running server.
    [[nodiscard]] bool tlsEnabled() const noexcept {
        return tls_.has_value();
    }

    /// The route paths the backend serves, in declaration order. Exposed so
    /// a unit test can assert the route table has no duplicate paths without
    /// reaching into the private table.
    [[nodiscard]] static std::vector<std::string_view> routePaths();

private:
    /// Non-virtual stop() body. Called from both the public virtual stop()
    /// and the destructor; a destructor must not call a virtual
    /// (clang-analyzer-optin.cplusplus.VirtualCall).
    void stopImpl() noexcept;

    /// Bind every route in the table plus the 404 + exception handlers onto
    /// `server_`. Called once per start().
    void registerRoutes();

    /// Create the concrete server for this start(): an `httplib::SSLServer`
    /// when TLS is configured, a plain `httplib::Server` otherwise. Both
    /// derive from the `httplib::Server` the rest of the class talks to, so
    /// the route table and the accept loop are identical either way.
    ///
    /// @throws core::TlsMaterialError when the SSL context cannot be built
    ///         from already-validated material (an OpenSSL-level refusal).
    [[nodiscard]] std::unique_ptr<httplib::Server> makeServer() const;

    /// "off", "server" or "mutual". What the port actually speaks, for the
    /// start() log line and the metrics summary.
    [[nodiscard]] const char* tlsModeName() const noexcept;

    std::uint16_t requestedPort_;
    std::string   bindAddress_;

    /// Proven TLS material, or nothing for plaintext HTTP. Holding the
    /// material (not the raw options) means the paths the server opens in
    /// start() are exactly the ones validated in the constructor.
    std::optional<HttpTlsMaterial> tls_;

    model::ProductionModel&    production_;
    model::ProductsRepository& products_;
    presenter::AlertCenter&    alerts_;
    core::Logger&              logger_;

    std::unique_ptr<httplib::Server> server_;
    std::jthread                     thread_;
    std::atomic<bool>                running_{false};
    std::atomic<std::uint16_t>       boundPort_{0};
};

}  // namespace app::integration
