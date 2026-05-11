#include "src/integration/opcua/Open62541Client.h"

#include "src/core/LoggerBase.h"

// open62541 is a C library; its types collide with anything that
// pulls Windows headers. Include it after our own non-Windows
// dependencies and keep its surface inside this TU.
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/types.h>

#include <chrono>
#include <format>
#include <stdexcept>
#include <utility>
#include <vector>

namespace app::integration::opcua {

namespace {

/// Iterate timeout the worker hands to `UA_Client_run_iterate` per
/// pass. 0 is a non-blocking poll; the worker thread itself sleeps
/// between passes for `iterateInterval`. The combination gives a
/// predictable cadence without spinning a CPU.
constexpr std::uint32_t kIteratePollTimeoutMs = 0;

}  // namespace

/// Pimpl: keeps the open62541 C handle out of the public header so
/// the rest of the codebase compiles without pulling `<open62541/...>`
/// into every TU. Methods that mutate `client` must run on the
/// worker thread only (UA_Client is not thread-safe).
struct Open62541Client::Impl {
    UA_Client* client = nullptr;
};

Open62541Client::Open62541Client(Config config, core::Logger& logger)
    : config_(std::move(config)),
      logger_(logger),
      impl_(std::make_unique<Impl>()) {}

Open62541Client::~Open62541Client() {
    stopImpl();
}

void Open62541Client::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;  // idempotent
    }

    try {
        connectSync();
    } catch (...) {
        // Roll back so isRunning() reflects reality and start() can
        // be retried once the server is reachable. Mirrors
        // MqttClient's connect-failure path.
        running_.store(false, std::memory_order_release);
        if (impl_->client != nullptr) {
            UA_Client_delete(impl_->client);
            impl_->client = nullptr;
        }
        throw;
    }

    replaySubscriptions();

    thread_ = std::jthread([this](std::stop_token stop) {
        runIoLoop(std::move(stop));
    });
}

void Open62541Client::stop() {
    stopImpl();
}

void Open62541Client::stopImpl() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    // Signal the worker to stop iterating. Joining via jthread RAII
    // happens at thread_'s destruction; we trigger the stop first so
    // the worker doesn't loop one extra time after the client is
    // gone.
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }

    if (impl_->client != nullptr) {
        // Disconnect is best-effort: even if the server is already
        // gone, we still want to free the local handle. open62541
        // documents `UA_Client_disconnect` as safe to call on a
        // half-open or already-disconnected client.
        (void)UA_Client_disconnect(impl_->client);
        UA_Client_delete(impl_->client);
        impl_->client = nullptr;
    }

    monitoredItemCount_.store(0, std::memory_order_release);
}

bool Open62541Client::isRunning() const {
    return running_.load(std::memory_order_acquire);
}

std::string Open62541Client::metricsSummary() const {
    return std::format(
        "endpoint {} | {} monitored",
        config_.endpointUrl,
        monitoredItemCount_.load(std::memory_order_acquire));
}

integration::BackendState
Open62541Client::connectionState() const noexcept {
    // Connected only after CONNECT/CREATESESSION succeeded on the
    // worker thread. start() flips `running_` true after the
    // synchronous handshake; if open62541 later reports the channel
    // closed we set `running_` to false on the worker thread and
    // surface Degraded via the monitored-item count (a previously
    // armed item means we DID reach Connected at least once).
    if (running_.load(std::memory_order_acquire)) {
        return integration::BackendState::Connected;
    }
    if (monitoredItemCount_.load(std::memory_order_acquire) > 0) {
        return integration::BackendState::Degraded;
    }
    return integration::BackendState::Disconnected;
}

void Open62541Client::connectSync() {
    impl_->client = UA_Client_new();
    if (impl_->client == nullptr) {
        throw std::runtime_error(
            "Open62541Client: UA_Client_new returned null");
    }
    UA_ClientConfig* clientConfig = UA_Client_getConfig(impl_->client);
    UA_StatusCode rc = UA_ClientConfig_setDefault(clientConfig);
    if (rc != UA_STATUSCODE_GOOD) {
        throw std::runtime_error(std::format(
            "Open62541Client: setDefault failed: {}",
            UA_StatusCode_name(rc)));
    }

    rc = UA_Client_connect(impl_->client, config_.endpointUrl.c_str());
    if (rc != UA_STATUSCODE_GOOD) {
        throw std::runtime_error(std::format(
            "Open62541Client: connect to {} failed: {}",
            config_.endpointUrl, UA_StatusCode_name(rc)));
    }

    logger_.info("OPC-UA client connected to {}", config_.endpointUrl);
}

void Open62541Client::createSubscription() {
    // Subscription + monitored-item creation land in the next commit.
    // The wire surface is here so commit 2 doesn't have to touch this
    // header again.
}

void Open62541Client::replaySubscriptions() {
    // Same -- subscribe()'s storage path is wired (registerSubscription
    // below), but arming on the server is commit 2.
}

void Open62541Client::runIoLoop(std::stop_token stop) {
    // Single thread owns the UA_Client for its entire lifetime.
    // We loop iterate + sleep until stopped; iterate returns the
    // last status code so we can surface a hard channel break as
    // `running_ = false` -> Degraded pill.
    while (!stop.stop_requested()) {
        const UA_StatusCode rc =
            UA_Client_run_iterate(impl_->client, kIteratePollTimeoutMs);
        if (rc != UA_STATUSCODE_GOOD &&
            rc != UA_STATUSCODE_GOODNODATA) {
            // Hard transport failure (server gone, channel reset).
            // Surface Degraded by dropping running_ so the I/O panel
            // pill flips off-green; a future reconnect path can roll
            // it back to Connected.
            running_.store(false, std::memory_order_release);
            return;
        }
        std::this_thread::sleep_for(config_.iterateInterval);
    }
}

bool Open62541Client::subscribeFloat(std::string_view nodeBrowsePath,
                                      FloatCallback callback) {
    PendingSubscription p;
    p.nodeBrowsePath = std::string{nodeBrowsePath};
    p.type           = ValueType::Float;
    p.floatCb        = std::move(callback);
    return registerSubscription(std::move(p));
}

bool Open62541Client::subscribeInt32(std::string_view nodeBrowsePath,
                                      Int32Callback callback) {
    PendingSubscription p;
    p.nodeBrowsePath = std::string{nodeBrowsePath};
    p.type           = ValueType::Int32;
    p.int32Cb        = std::move(callback);
    return registerSubscription(std::move(p));
}

bool Open62541Client::subscribeBool(std::string_view nodeBrowsePath,
                                     BoolCallback callback) {
    PendingSubscription p;
    p.nodeBrowsePath = std::string{nodeBrowsePath};
    p.type           = ValueType::Bool;
    p.boolCb         = std::move(callback);
    return registerSubscription(std::move(p));
}

std::size_t Open62541Client::monitoredItemCount() const noexcept {
    return monitoredItemCount_.load(std::memory_order_acquire);
}

bool Open62541Client::registerSubscription(PendingSubscription pending) {
    if (pending.nodeBrowsePath.empty()) return false;

    // Always store -- if we're not running yet, replaySubscriptions()
    // will pick it up post-connect; if we are running, the next
    // commit arms it on the worker thread.
    const std::lock_guard<std::mutex> lock(subscriptionsMutex_);
    subscriptions_.push_back(std::move(pending));
    return true;
}

bool Open62541Client::armMonitoredItem(
    const PendingSubscription& /*pending*/) {
    // Commit 2 wires UA_Client_MonitoredItems_createDataChange here.
    return false;
}

}  // namespace app::integration::opcua
