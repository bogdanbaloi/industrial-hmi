#include "src/integration/opcua/Open62541Client.h"

#include "src/core/LoggerBase.h"

// open62541 is a C library; its types collide with anything that
// pulls Windows headers. Include it after our own non-Windows
// dependencies and keep its surface inside this TU.
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_subscriptions.h>
#include <open62541/types.h>

#include <chrono>
#include <format>
#include <memory>
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

/// Application-defined namespace index, matching the server we ship.
/// All Factory nodes live under ns=1; standard root references live
/// under ns=0 and are addressed via UA_NS0ID constants.
constexpr std::uint16_t kApplicationNamespace = 1U;

/// Parsed browse path: slash-separated string -> N qualified names,
/// each tagged with the application namespace. Owns the segment
/// storage because UA_QualifiedName holds a non-owning pointer into
/// it that must outlive the request.
struct BrowsePathParts {
    std::vector<std::string>      storage;
    std::vector<UA_QualifiedName> qualified;
};

BrowsePathParts parseBrowsePath(std::string_view path) {
    BrowsePathParts parts;
    std::size_t cursor = 0;
    while (cursor < path.size()) {
        const auto next = path.find('/', cursor);
        const auto end = (next == std::string_view::npos)
                             ? path.size() : next;
        parts.storage.emplace_back(path.substr(cursor, end - cursor));
        cursor = (next == std::string_view::npos) ? path.size() : next + 1;
    }
    parts.qualified.reserve(parts.storage.size());
    for (auto& segment : parts.storage) {
        parts.qualified.push_back(
            UA_QUALIFIEDNAME(kApplicationNamespace, segment.data()));
    }
    return parts;
}

/// Client-side translate-browse-paths. Returns `UA_NODEID_NULL` if
/// the path doesn't resolve (caller must check via `UA_NodeId_isNull`).
UA_NodeId resolveNode(UA_Client* client, std::string_view path) {
    auto parts = parseBrowsePath(path);
    if (parts.qualified.empty()) return UA_NODEID_NULL;

    UA_BrowsePath bp;
    UA_BrowsePath_init(&bp);
    bp.startingNode = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);

    std::vector<UA_RelativePathElement> elements(parts.qualified.size());
    for (std::size_t i = 0; i < parts.qualified.size(); ++i) {
        UA_RelativePathElement_init(&elements[i]);
        elements[i].referenceTypeId =
            UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
        elements[i].isInverse       = false;
        elements[i].includeSubtypes = true;
        elements[i].targetName      = parts.qualified[i];
    }
    bp.relativePath.elementsSize = parts.qualified.size();
    bp.relativePath.elements     = elements.data();

    UA_TranslateBrowsePathsToNodeIdsRequest req;
    UA_TranslateBrowsePathsToNodeIdsRequest_init(&req);
    req.browsePathsSize = 1;
    req.browsePaths     = &bp;

    UA_TranslateBrowsePathsToNodeIdsResponse resp =
        UA_Client_Service_translateBrowsePathsToNodeIds(client, req);

    UA_NodeId resolved = UA_NODEID_NULL;
    if (resp.responseHeader.serviceResult == UA_STATUSCODE_GOOD &&
        resp.resultsSize > 0 &&
        resp.results[0].statusCode == UA_STATUSCODE_GOOD &&
        resp.results[0].targetsSize > 0) {
        UA_NodeId_copy(&resp.results[0].targets[0].targetId.nodeId, &resolved);
    }
    UA_TranslateBrowsePathsToNodeIdsResponse_clear(&resp);
    // No request_clear -- elements live on stack, we own them.
    return resolved;
}

}  // namespace


/// Pimpl: keeps the open62541 C handle out of the public header so
/// the rest of the codebase compiles without pulling `<open62541/...>`
/// into every TU. Methods that mutate `client` must run on the
/// worker thread only (UA_Client is not thread-safe).
struct Open62541Client::Impl {
    UA_Client*    client          = nullptr;
    /// Server-assigned subscription identifier returned by
    /// `Subscriptions_create`. 0 means "no subscription armed yet".
    UA_UInt32     subscriptionId  = 0;
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
        createSubscription();
    } catch (...) {
        // Roll back so isRunning() reflects reality and start() can
        // be retried once the server is reachable. Mirrors
        // MqttClient's connect-failure path.
        running_.store(false, std::memory_order_release);
        if (impl_->client != nullptr) {
            UA_Client_delete(impl_->client);
            impl_->client         = nullptr;
            impl_->subscriptionId = 0;
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
        if (impl_->subscriptionId != 0) {
            // Best-effort: the broker may already be gone, in which
            // case the delete request fails silently. We zero the id
            // regardless so a future start() doesn't try to reuse it.
            (void)UA_Client_Subscriptions_deleteSingle(
                impl_->client, impl_->subscriptionId);
            impl_->subscriptionId = 0;
        }
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
    if (impl_->subscriptionId != 0) return;  // already armed

    UA_CreateSubscriptionRequest req = UA_CreateSubscriptionRequest_default();
    req.requestedPublishingInterval = config_.publishingIntervalMs;

    UA_CreateSubscriptionResponse resp =
        UA_Client_Subscriptions_create(
            impl_->client, req,
            /*subscriptionContext=*/nullptr,
            /*statusChangeCallback=*/nullptr,
            /*deleteCallback=*/nullptr);
    if (resp.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
        throw std::runtime_error(std::format(
            "Open62541Client: Subscriptions_create failed: {}",
            UA_StatusCode_name(resp.responseHeader.serviceResult)));
    }
    impl_->subscriptionId = resp.subscriptionId;
}

void Open62541Client::replaySubscriptions() {
    // Snapshot the pending list under the lock so a concurrent
    // subscribe() doesn't trip the iteration. Each entry stays
    // owned by `subscriptions_`; we just borrow the raw pointer.
    std::vector<PendingSubscription*> snapshot;
    {
        const std::lock_guard<std::mutex> lock(subscriptionsMutex_);
        snapshot.reserve(subscriptions_.size());
        for (auto& p : subscriptions_) snapshot.push_back(p.get());
    }
    for (auto* p : snapshot) {
        (void)armMonitoredItem(*p);
    }
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
    // will pick it up post-connect; if we are running, we arm right
    // away on the caller's thread (UA_Client API used here is safe
    // because the worker only ever calls `UA_Client_run_iterate` and
    // open62541 documents these "service" calls as composable).
    PendingSubscription* stored = nullptr;
    {
        const std::lock_guard<std::mutex> lock(subscriptionsMutex_);
        auto owned = std::make_unique<PendingSubscription>(std::move(pending));
        stored = owned.get();
        subscriptions_.push_back(std::move(owned));
    }
    if (running_.load(std::memory_order_acquire)) {
        return armMonitoredItem(*stored);
    }
    return true;  // queued; will arm at next start()
}

namespace {

/// C callback open62541 invokes on every data-change notification.
/// `monContext` is the `PendingSubscription*` we set when creating
/// the monitored item; dispatch the typed application callback based
/// on the variant payload.
//
// open62541's signature is fixed by the spec; clang-tidy's parameter-
// naming rule fires on snake_case there. NOLINT keeps the diff
// localised.
// NOLINTNEXTLINE(readability-named-parameter,readability-identifier-naming,bugprone-easily-swappable-parameters)
void onDataChange(UA_Client* /*client*/,
                  UA_UInt32  /*subId*/,
                  void*      /*subContext*/,
                  UA_UInt32  /*monId*/,
                  void*       monContext,
                  UA_DataValue* value) {
    if (monContext == nullptr || value == nullptr) return;
    if (!value->hasValue) return;
    auto* pending = static_cast<Open62541Client::PendingSubscription*>(
        monContext);

    const UA_Variant& var = value->value;
    using ValueType = Open62541Client::ValueType;
    switch (pending->type) {
        using enum ValueType;
    case Float:
        if (UA_Variant_hasScalarType(&var, &UA_TYPES[UA_TYPES_FLOAT]) &&
            pending->floatCb) {
            const auto v = *static_cast<UA_Float*>(var.data);
            pending->floatCb(pending->nodeBrowsePath, v);
        }
        break;
    case Int32:
        if (UA_Variant_hasScalarType(&var, &UA_TYPES[UA_TYPES_INT32]) &&
            pending->int32Cb) {
            const auto v = *static_cast<UA_Int32*>(var.data);
            pending->int32Cb(pending->nodeBrowsePath, v);
        }
        break;
    case Bool:
        if (UA_Variant_hasScalarType(&var, &UA_TYPES[UA_TYPES_BOOLEAN]) &&
            pending->boolCb) {
            const auto v = *static_cast<UA_Boolean*>(var.data);
            pending->boolCb(pending->nodeBrowsePath, v != 0);
        }
        break;
    }
}

}  // namespace

bool Open62541Client::armMonitoredItem(const PendingSubscription& pending) {
    if (impl_->client == nullptr || impl_->subscriptionId == 0) return false;

    UA_NodeId nodeId = resolveNode(impl_->client, pending.nodeBrowsePath);
    if (UA_NodeId_isNull(&nodeId)) {
        logger_.warn(
            "OPC-UA client: node not found for browse path '{}'",
            pending.nodeBrowsePath);
        return false;
    }

    UA_MonitoredItemCreateRequest req =
        UA_MonitoredItemCreateRequest_default(nodeId);
    UA_MonitoredItemCreateResult res =
        UA_Client_MonitoredItems_createDataChange(
            impl_->client, impl_->subscriptionId,
            UA_TIMESTAMPSTORETURN_BOTH, req,
            // Const-cast: open62541's API takes a non-const void* for
            // the monitored-item context even though it only stores
            // the pointer and hands it back to us in the callback.
            // We never mutate `pending` through this pointer; the
            // const cast is the lesser evil compared to dropping
            // const everywhere upstream.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
            const_cast<void*>(static_cast<const void*>(&pending)),
            &onDataChange,
            /*deleteCallback=*/nullptr);
    UA_NodeId_clear(&nodeId);

    if (res.statusCode != UA_STATUSCODE_GOOD) {
        logger_.warn(
            "OPC-UA client: monitored-item create failed for '{}': {}",
            pending.nodeBrowsePath, UA_StatusCode_name(res.statusCode));
        return false;
    }
    monitoredItemCount_.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

}  // namespace app::integration::opcua
