#include "src/integration/TcpBackend.h"

#include "src/integration/JsonSerializer.h"
#include "src/model/ProductionTypes.h"

#include <boost/asio.hpp>

// Boost.Asio on Windows pulls in <windows.h> which #defines ERROR as 0.
// That breaks `case model::SystemState::ERROR:` further down (the enum
// constant turns into `case 0` after preprocessing). Undefine here --
// no other code in this TU needs the wingdi ERROR macro.
#ifdef ERROR
#  undef ERROR
#endif

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace app::integration {

namespace asio = boost::asio;
using boost::asio::ip::tcp;

namespace {

/// Strip leading + trailing ASCII whitespace.
std::string trim(std::string s) {
    auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

/// Tokenize on ASCII spaces. Empty tokens are dropped.
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) out.push_back(std::move(tok));
    return out;
}

/// Parse an unsigned 32-bit decimal id (`1`, `42`). Returns std::nullopt
/// on malformed input rather than throwing -- the caller prefers a
/// graceful "ERR bad id" response over an unwound stack.
std::optional<std::uint32_t> parseId(const std::string& s) {
    std::uint32_t out = 0;
    const auto* first = s.data();
    const auto* last  = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(first, last, out);
    if (ec != std::errc{} || ptr != last) return std::nullopt;
    return out;
}

/// Map SystemState to a stable wire-protocol string. Lower-case + no
/// whitespace so the value reads cleanly as a JSON string field.
const char* systemStateName(model::SystemState s) {
    using enum model::SystemState;
    switch (s) {
        case IDLE:        return "idle";
        case RUNNING:     return "running";
        case ERROR:       return "error";
        case CALIBRATION: return "calibration";
    }
    return "unknown";
}

/// Marshal a single Product as one JSON line for the `products` stream.
/// Reuses JsonSerializer for the field formatting / escape rules so we
/// don't drift from the file-export format.
std::string productToJsonLine(const model::Product& p) {
    JsonSerializer s;
    std::ostringstream out;
    s.writeProducts(out, {p});

    // JsonSerializer pretty-prints across multiple lines. For the line
    // protocol we need one record per output line, so collapse the
    // pretty-printed array `[\n  {\n    ...\n  }\n]\n` to a flat
    // `{...}` representation by stripping the surrounding brackets and
    // newlines. Cheaper than maintaining a second JSON formatter.
    std::string body = out.str();
    // Drop surrounding `[\n` and `\n]\n`.
    if (!body.empty() && body.front() == '[') body.erase(0, 1);
    while (!body.empty() && (body.back() == '\n' || body.back() == ']')) {
        body.pop_back();
    }
    // Collapse remaining whitespace runs to a single space for compactness.
    std::string flat;
    flat.reserve(body.size());
    bool prevSpace = false;
    for (char c : body) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ') {
            if (prevSpace) continue;
            prevSpace = true;
        } else {
            prevSpace = false;
        }
        flat += c;
    }
    return trim(std::move(flat));
}

}  // namespace

TcpBackend::TcpBackend(std::uint16_t port,
                       model::ProductionModel& production,
                       model::ProductsRepository& products)
    : requestedPort_(port),
      production_(production),
      products_(products),
      io_(std::make_unique<asio::io_context>()) {}

TcpBackend::~TcpBackend() {
    // Defensive -- caller should have called stop() explicitly. We use
    // the non-virtual stopImpl() rather than the virtual stop() because
    // calling a virtual from a destructor bypasses dynamic dispatch
    // (clang-analyzer-optin.cplusplus.VirtualCall) and is brittle if a
    // future subclass overrides stop().
    stopImpl();
}

void TcpBackend::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;  // already running, idempotent
    }

    // Bind synchronously so the caller learns about EADDRINUSE before
    // start() returns (per the IntegrationBackend contract). Acceptor
    // is then handed off to the worker thread which drives the
    // io_context.
    auto acceptor = std::make_shared<tcp::acceptor>(*io_);
    try {
        tcp::endpoint endpoint(tcp::v4(), requestedPort_);
        acceptor->open(endpoint.protocol());
        acceptor->set_option(tcp::acceptor::reuse_address(true));
        acceptor->bind(endpoint);
        acceptor->listen();
        boundPort_.store(acceptor->local_endpoint().port(),
                         std::memory_order_release);
    } catch (...) {
        running_.store(false, std::memory_order_release);
        throw;
    }

    // Start the accept loop. Captures `acceptor` by shared_ptr so it
    // outlives the lambda. Each accepted socket is handled
    // synchronously on the same io_context thread (one-client-at-a-
    // time is fine for a portfolio HMI; production deployments would
    // spawn a coroutine per connection).
    auto* self = this;
    auto acceptLoop = std::make_shared<std::function<void()>>();
    *acceptLoop = [self, acceptor, acceptLoop]() {
        acceptor->async_accept(
            [self, acceptor, acceptLoop](
                const boost::system::error_code& ec,
                tcp::socket socket) {
                if (ec) {
                    // Acceptor was cancelled (stop() called) or
                    // unrecoverable accept error. Either way, exit
                    // the loop -- the io_context will drain naturally.
                    return;
                }
                // Move the socket onto the heap so its lifetime spans
                // the synchronous handleConnection call. Releasing it
                // by value into the handler would close the fd as
                // soon as the lambda returned.
                auto socketPtr = std::make_unique<tcp::socket>(std::move(socket));
                self->handleConnection(socketPtr.release());
                // Re-arm for the next client.
                (*acceptLoop)();
            });
    };
    (*acceptLoop)();

    // Start the worker thread. jthread auto-joins on stop() / dtor.
    thread_ = std::jthread([this]() { runIoLoop(); });
}

void TcpBackend::stop() {
    stopImpl();
}

void TcpBackend::stopImpl() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;  // already stopped, idempotent
    }
    // Request the io_context to exit, then join the worker thread.
    try {
        if (io_) io_->stop();
        if (thread_.joinable()) thread_.join();
        boundPort_.store(0, std::memory_order_release);

        // Reset io_context so a future start() begins from a clean slate.
        io_ = std::make_unique<asio::io_context>();
    }
    // Shutdown is noexcept by contract; logger is gone by this point
    // and there's nowhere meaningful to surface a stop-time failure.
    // NOLINTNEXTLINE(bugprone-empty-catch)
    catch (...) { /* swallow */ }
}

void TcpBackend::runIoLoop() {
    try {
        io_->run();
    } catch (...) {
        // Worker thread can't propagate; mark backend down so callers
        // notice via isRunning(). Real production wiring would also
        // log here through an injected logger.
        running_.store(false, std::memory_order_release);
    }
}

void TcpBackend::handleConnection(void* tcpSocketPtr) {
    // Adopt the socket the caller released via unique_ptr::release().
    std::unique_ptr<tcp::socket> socket(static_cast<tcp::socket*>(tcpSocketPtr));
    if (!socket) return;

    boost::system::error_code ec;
    asio::streambuf buf;
    while (running_.load(std::memory_order_acquire)) {
        const std::size_t n = asio::read_until(*socket, buf, '\n', ec);
        if (ec || n == 0) break;

        std::istream is(&buf);
        std::string line;
        std::getline(is, line);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim(line);
        if (line.empty()) continue;

        std::string response;
        const bool keepOpen = dispatchCommand(line, response);

        asio::write(*socket, asio::buffer(response), ec);
        if (ec || !keepOpen) break;
    }

    // No explicit close() -- the unique_ptr's destructor closes the
    // socket cleanly when it goes out of scope. Letting RAII do the
    // work avoids both bugprone-unused-return-value (close()'s ec
    // overload signals errors via the parameter, but clang-tidy still
    // flags the call) and any double-close scenarios.
}

bool TcpBackend::dispatchCommand(const std::string& line, std::string& out) {
    const auto tokens = tokenize(line);
    if (tokens.empty()) {
        out = "ERR empty command\n";
        return true;
    }
    const std::string& cmd = tokens[0];

    if (cmd == "quit" || cmd == "exit") {
        out = "BYE\n";
        return false;
    }

    if (cmd == "help") {
        out =
            "Available commands:\n"
            "  status                    JSON snapshot (running, equipment count)\n"
            "  products                  count line, then N JSON product objects\n"
            "  eq <id> on|off            toggle equipment (id is unsigned int)\n"
            "  production start|stop|reset\n"
            "  help                      this banner\n"
            "  quit                      close the connection\n";
        return true;
    }

    if (cmd == "status") {
        const auto state = production_.getState();
        const bool isRunning = state == model::SystemState::RUNNING;
        out = std::format(
            R"({{"state":"{}","running":{}}})" "\n",
            systemStateName(state),
            isRunning ? "true" : "false");
        return true;
    }

    if (cmd == "products") {
        const auto products = products_.getAllProducts();
        out = std::format("{}\n", products.size());
        for (const auto& p : products) {
            out += productToJsonLine(p);
            out += '\n';
        }
        return true;
    }

    if (cmd == "eq") {
        if (tokens.size() != 3) {
            out = "ERR usage: eq <id> on|off\n";
            return true;
        }
        const auto id = parseId(tokens[1]);
        if (!id) {
            out = "ERR bad id (expected unsigned integer)\n";
            return true;
        }
        if (tokens[2] == "on") {
            production_.setEquipmentEnabled(*id, true);
        } else if (tokens[2] == "off") {
            production_.setEquipmentEnabled(*id, false);
        } else {
            out = "ERR expected on|off\n";
            return true;
        }
        out = "OK\n";
        return true;
    }

    if (cmd == "production") {
        if (tokens.size() != 2) {
            out = "ERR usage: production start|stop|reset\n";
            return true;
        }
        if (tokens[1] == "start")      production_.startProduction();
        else if (tokens[1] == "stop")  production_.stopProduction();
        else if (tokens[1] == "reset") production_.resetSystem();
        else {
            out = "ERR expected start|stop|reset\n";
            return true;
        }
        out = "OK\n";
        return true;
    }

    out = std::format("ERR unknown command: {}\n", cmd);
    return true;
}

}  // namespace app::integration
