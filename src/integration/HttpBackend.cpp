#include "src/integration/HttpBackend.h"

#include "src/core/LoggerBase.h"
#include "src/core/StartupErrors.h"
#include "src/integration/ProductionMetricsJson.h"
#include "src/integration/StatusJson.h"
#include "src/mcp/tools/AlarmsSnapshotTool.h"
#include "src/model/Product.h"
#include "src/model/ProductsRepository.h"

#include <nlohmann/json.hpp>

// cpp-httplib pulls <winsock2.h>/<windows.h> on Windows. Keep those from
// defining the min/max function macros (they collide with std::min/max used
// by the standard library and nlohmann) and trim the header. httplib.h is
// included LAST so its Windows macros can't leak into the headers above.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif
#include <httplib.h>
// <windows.h> (via httplib on Windows) #defines ERROR as 0; undo it so the
// token stays clean for the rest of this TU. Nothing here needs the wingdi
// ERROR macro.
#ifdef ERROR
#  undef ERROR
#endif

#include <array>
#include <chrono>
#include <exception>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace app::integration {

namespace {

// Route paths -- named constants, never inline string literals. The five
// together form the authoritative route table: `registerRoutes` binds a
// handler to each and `HttpBackend::routePaths` reports them for the
// duplicate-path test.
namespace routes {
inline constexpr const char* kHealth     = "/health";
inline constexpr const char* kStatus     = "/status";
inline constexpr const char* kAlarms     = "/alarms";
inline constexpr const char* kProducts   = "/products";
inline constexpr const char* kProduction = "/production";
}  // namespace routes

inline constexpr std::array<const char*, 5> kRouteTable = {
    routes::kHealth,
    routes::kStatus,
    routes::kAlarms,
    routes::kProducts,
    routes::kProduction,
};

// Response content type + fixed / error bodies. Named so no bare JSON or
// mime literal is scattered through the handlers.
inline constexpr const char* kJsonMime          = "application/json";
inline constexpr const char* kHealthBody        = R"({"status":"ok"})";
inline constexpr const char* kNotFoundBody      = R"({"error":"not found"})";
inline constexpr const char* kInternalErrorBody = R"({"error":"internal error"})";

// The TLS mode reported by metricsSummary() and the start() log line. Named
// rather than inline literals because each one is emitted from more than one
// place, and "off" vs "server" vs "mutual" is the operator's only evidence of
// what the port actually speaks.
namespace tls_mode {
inline constexpr const char* kOff        = "off";
inline constexpr const char* kServerOnly = "server";
inline constexpr const char* kMutual     = "mutual";
}  // namespace tls_mode

// URL scheme for the start() log line, so the trace matches what a client
// has to type.
inline constexpr const char* kPlaintextScheme = "http";
inline constexpr const char* kTlsScheme       = "https";

// HTTP status codes we set explicitly (httplib defaults unmatched routes to
// 404; we set 500 ourselves on a handler exception). Named to keep the magic
// numbers out of the handler bodies.
inline constexpr int kStatusNotFound      = 404;
inline constexpr int kStatusInternalError = 500;

// Bounded wait for the accept loop to come up after start() spins its
// thread, so callers (and tests) observe a server that is actually ready to
// answer once start() returns.
inline constexpr std::chrono::milliseconds kReadyPollInterval{5};
inline constexpr int                       kReadyPollMaxAttempts = 400;  // ~2s

/// Marshal the product catalogue to a JSON array. Field set + names follow
/// JsonSerializer's conventions (productCode / name / status / stock /
/// qualityRate) so the REST shape matches the file-export and TCP shapes.
nlohmann::json productsToJson(model::ProductsRepository& products) {
    nlohmann::json array = nlohmann::json::array();
    for (const auto& p : products.getAllProducts()) {
        array.push_back({
            {"productCode", p.productCode},
            {"name", p.name},
            {"status", p.status},
            {"stock", p.stock},
            {"qualityRate", p.qualityRate},
        });
    }
    return array;
}

}  // namespace

HttpBackend::HttpBackend(std::uint16_t port,
                         std::string bindAddress,
                         model::ProductionModel& production,
                         model::ProductsRepository& products,
                         presenter::AlertCenter& alerts,
                         core::Logger& logger,
                         std::optional<HttpTlsOptions> tlsOptions)
    : requestedPort_(port),
      bindAddress_(std::move(bindAddress)),
      production_(production),
      products_(products),
      alerts_(alerts),
      logger_(logger) {
    if (tlsOptions.has_value()) {
        // Load + verify the cert / key (/ client CA) NOW, before anything can
        // bind a port. A failure throws core::TlsMaterialError out of this
        // constructor, up through registerHttpBackend() to the top-level
        // catch in main(), exactly like a bad config or a dead database
        // (ADR-0030). There is no plaintext fallback on purpose.
        tls_.emplace(std::move(*tlsOptions));
    }
}

HttpBackend::~HttpBackend() {
    // Defensive -- callers should stop() explicitly. Use the non-virtual
    // stopImpl() because calling a virtual from a destructor bypasses dynamic
    // dispatch (clang-analyzer-optin.cplusplus.VirtualCall).
    stopImpl();
}

std::vector<std::string_view> HttpBackend::routePaths() {
    return {kRouteTable.begin(), kRouteTable.end()};
}

void HttpBackend::registerRoutes() {
    // GET /health -- liveness, no model access.
    server_->Get(routes::kHealth,
                 [](const httplib::Request&, httplib::Response& res) {
                     res.set_content(kHealthBody, kJsonMime);
                 });

    // GET /status -- shared shape with the TCP `status` command.
    server_->Get(routes::kStatus,
                 [this](const httplib::Request&, httplib::Response& res) {
                     res.set_content(buildStatusJson(production_).dump(),
                                     kJsonMime);
                 });

    // GET /alarms -- the exact projection the MCP `alarms_snapshot` tool
    // serves, reused verbatim so the two consumers cannot drift.
    server_->Get(routes::kAlarms,
                 [this](const httplib::Request&, httplib::Response& res) {
                     res.set_content(app::mcp::runAlarmsSnapshot(alerts_).dump(),
                                     kJsonMime);
                 });

    // GET /products -- the product catalogue as a JSON array.
    server_->Get(routes::kProducts,
                 [this](const httplib::Request&, httplib::Response& res) {
                     res.set_content(productsToJson(products_).dump(),
                                     kJsonMime);
                 });

    // GET /production -- throughput + OEE + derived minutesPerUnit, the exact
    // shape the MCP `production_metrics` tool serves, reused through the shared
    // buildProductionMetricsJson builder so the two consumers cannot drift.
    server_->Get(routes::kProduction,
                 [this](const httplib::Request&, httplib::Response& res) {
                     res.set_content(
                         buildProductionMetricsJson(production_).dump(),
                         kJsonMime);
                 });

    // Unmatched path -> 404 with a JSON body. httplib has already set the
    // 404 status and calls this for any >= 400 response, so we scope the
    // synthesized body to a genuine not-found with no body of its own (the
    // 500 path below supplies its own body and is left untouched).
    server_->set_error_handler(
        [](const httplib::Request&, httplib::Response& res) {
            if (res.status == kStatusNotFound && res.body.empty()) {
                res.set_content(kNotFoundBody, kJsonMime);
            }
        });

    // Any exception escaping a handler (e.g. a model read that threw) is
    // logged and mapped to 500 -- never silently swallowed, never allowed to
    // tear down the accept loop.
    server_->set_exception_handler(
        [this](const httplib::Request& req, httplib::Response& res,
               const std::exception_ptr& ep) {
            std::string reason;
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                reason = e.what();
            } catch (...) {
                reason = "non-std exception";
            }
            logger_.error("HTTP handler for {} failed: {}", req.path, reason);
            res.status = kStatusInternalError;
            res.set_content(kInternalErrorBody, kJsonMime);
        });
}

std::unique_ptr<httplib::Server> HttpBackend::makeServer() const {
    if (!tls_.has_value()) {
        return std::make_unique<httplib::Server>();
    }

    const HttpTlsOptions& options = tls_->options();
    // A null client-CA path is httplib's "do not ask the client for a
    // certificate"; passing the CA turns on SSL_VERIFY_PEER |
    // SSL_VERIFY_FAIL_IF_NO_PEER_CERT, i.e. mutual TLS.
    const char* clientCa =
        tls_->verifiesPeer() ? options.clientCaPath.c_str() : nullptr;

    auto server = std::make_unique<httplib::SSLServer>(
        options.certPath.c_str(), options.keyPath.c_str(), clientCa);

    // The material was already proven in the constructor, so this is the
    // residual case where OpenSSL itself refused to build a context. Still
    // fatal -- never fall through to a plaintext server.
    if (!server->is_valid()) {
        throw core::TlsMaterialError(std::format(
            "HttpBackend: OpenSSL refused the TLS context built from {} / {}",
            options.certPath, options.keyPath));
    }
    return server;
}

void HttpBackend::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;  // already running, idempotent
    }

    try {
        server_ = makeServer();
    } catch (...) {
        running_.store(false, std::memory_order_release);
        throw;
    }
    registerRoutes();

    // Bind synchronously so the caller learns about a busy port before
    // start() returns (per the IntegrationBackend contract). port == 0 asks
    // the OS to assign a free port, reported through boundPort().
    int bound = 0;
    try {
        if (requestedPort_ == 0) {
            bound = server_->bind_to_any_port(bindAddress_);
            if (bound <= 0) {
                throw std::runtime_error(
                    "HttpBackend: bind_to_any_port failed on " + bindAddress_);
            }
        } else {
            if (!server_->bind_to_port(bindAddress_, requestedPort_)) {
                throw std::runtime_error(std::format(
                    "HttpBackend: bind failed on {}:{}", bindAddress_,
                    requestedPort_));
            }
            bound = requestedPort_;
        }
    } catch (...) {
        running_.store(false, std::memory_order_release);
        server_.reset();
        throw;
    }
    boundPort_.store(static_cast<std::uint16_t>(bound),
                     std::memory_order_release);

    // Run the accept loop on our own thread. jthread auto-joins on stop() /
    // dtor. listen_after_bind() blocks until Server::stop() is called.
    thread_ = std::jthread([this]() { server_->listen_after_bind(); });

    // Wait until the loop is actually accepting so a request issued right
    // after start() (as tests do) is not racing the listen thread.
    for (int attempt = 0;
         attempt < kReadyPollMaxAttempts && !server_->is_running(); ++attempt) {
        std::this_thread::sleep_for(kReadyPollInterval);
    }

    logger_.info("HTTP backend listening on {}://{}:{} (TLS {})",
                 tls_.has_value() ? kTlsScheme : kPlaintextScheme,
                 bindAddress_, boundPort_.load(std::memory_order_acquire),
                 tlsModeName());
}

const char* HttpBackend::tlsModeName() const noexcept {
    if (!tls_.has_value()) return tls_mode::kOff;
    return tls_->verifiesPeer() ? tls_mode::kMutual : tls_mode::kServerOnly;
}

std::string HttpBackend::metricsSummary() const {
    return std::format("port {}, tls {}",
                       boundPort_.load(std::memory_order_acquire),
                       tlsModeName());
}

void HttpBackend::stop() {
    stopImpl();
}

void HttpBackend::stopImpl() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;  // already stopped, idempotent
    }
    try {
        // stop() unblocks listen_after_bind() so the jthread can join.
        if (server_) server_->stop();
        if (thread_.joinable()) thread_.join();
        boundPort_.store(0, std::memory_order_release);
        server_.reset();
    }
    // Shutdown is noexcept by contract; the logger may be gone by dtor time
    // and there is nowhere meaningful to surface a stop-time failure.
    // NOLINTNEXTLINE(bugprone-empty-catch)
    catch (...) { /* swallow */ }
}

}  // namespace app::integration
