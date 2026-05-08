#include "src/integration/opcua/Open62541Server.h"

#include "src/core/LoggerBase.h"

// open62541 ships as a single amalgamated header. Suppress warnings
// from the C source -- our project treats them as errors but the
// upstream library has its own clean build.
#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wpedantic"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#if defined(__clang__) || defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace app::integration::opcua {

namespace {

// open62541's run loop wants a polling timeout in milliseconds. 100ms
// keeps stop() responsive without burning CPU on an empty server.
constexpr std::uint16_t kIterateTimeoutMs = 100;

/// Build a fully-qualified browse path from a slash-separated string,
/// rooted at the Objects folder. open62541 wants an array of qualified
/// names per call; the temporary lives long enough for the helper.
struct BrowsePathParts {
    std::vector<UA_QualifiedName> qualified;
    std::vector<std::string> storage;  // backs the qualified-name strings
};

BrowsePathParts parseBrowsePath(std::string_view path) {
    BrowsePathParts parts;
    std::size_t cursor = 0;
    while (cursor < path.size()) {
        const auto next = path.find('/', cursor);
        const auto end = (next == std::string_view::npos) ? path.size() : next;
        parts.storage.emplace_back(path.substr(cursor, end - cursor));
        cursor = (next == std::string_view::npos) ? path.size() : next + 1;
    }
    parts.qualified.reserve(parts.storage.size());
    for (auto& segment : parts.storage) {
        parts.qualified.push_back(UA_QUALIFIEDNAME(
            1, segment.data()));  // namespace 1 (application)
    }
    return parts;
}

/// Resolve a browse-path string under Objects/ to a NodeId. Returns
/// `UA_NODEID_NULL` if the path doesn't exist.
UA_NodeId resolveNode(UA_Server* server, std::string_view path) {
    auto parts = parseBrowsePath(path);
    if (parts.qualified.empty()) return UA_NODEID_NULL;

    UA_BrowsePath bp;
    UA_BrowsePath_init(&bp);
    bp.startingNode = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    bp.relativePath.elementsSize = parts.qualified.size();

    std::vector<UA_RelativePathElement> elements(parts.qualified.size());
    for (std::size_t i = 0; i < parts.qualified.size(); ++i) {
        UA_RelativePathElement_init(&elements[i]);
        elements[i].referenceTypeId =
            UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
        elements[i].isInverse = false;
        elements[i].includeSubtypes = true;
        elements[i].targetName = parts.qualified[i];
    }
    bp.relativePath.elements = elements.data();

    UA_BrowsePathResult result =
        UA_Server_translateBrowsePathToNodeIds(server, &bp);

    UA_NodeId resolved = UA_NODEID_NULL;
    if (result.statusCode == UA_STATUSCODE_GOOD && result.targetsSize > 0) {
        UA_NodeId_copy(&result.targets[0].targetId.nodeId, &resolved);
    }
    UA_BrowsePathResult_clear(&result);
    return resolved;
}

template <typename T>
bool writeScalar(UA_Server* server,
                 std::string_view path,
                 const UA_DataType& type,
                 const T& value) noexcept {
    if (server == nullptr) return false;
    UA_NodeId nodeId = resolveNode(server, path);
    if (UA_NodeId_isNull(&nodeId)) return false;

    UA_Variant var;
    UA_Variant_init(&var);
    // open62541's setScalar takes a non-const pointer but doesn't
    // mutate the value; const_cast is the documented usage in their
    // tutorials. The cast is safe because UA_Server_writeValue copies
    // the variant before queuing the write.
    UA_Variant_setScalar(&var, const_cast<T*>(&value), &type);

    const UA_StatusCode rc = UA_Server_writeValue(server, nodeId, var);
    UA_NodeId_clear(&nodeId);
    return rc == UA_STATUSCODE_GOOD;
}

}  // namespace

struct Open62541Server::Impl {
    UA_Server* server = nullptr;
};

Open62541Server::Open62541Server(OpcUaConfig config, core::Logger& logger)
    : config_(std::move(config)),
      logger_(logger),
      impl_(std::make_unique<Impl>()) {}

Open62541Server::~Open62541Server() {
    stop();
}

void Open62541Server::start() {
    if (running_.load(std::memory_order_acquire)) return;

    impl_->server = UA_Server_new();
    if (impl_->server == nullptr) {
        throw std::runtime_error(
            "Open62541Server: UA_Server_new returned null");
    }

    UA_ServerConfig* serverConfig = UA_Server_getConfig(impl_->server);
    UA_StatusCode rc = UA_ServerConfig_setMinimal(
        serverConfig, config_.port, /*certificate=*/nullptr);
    if (rc != UA_STATUSCODE_GOOD) {
        UA_Server_delete(impl_->server);
        impl_->server = nullptr;
        throw std::runtime_error(
            std::string("Open62541Server: setMinimal failed: ") +
            UA_StatusCode_name(rc));
    }

    // Set application URI + name from config so clients can identify
    // this server in their address books.
    UA_String_clear(&serverConfig->applicationDescription.applicationUri);
    serverConfig->applicationDescription.applicationUri =
        UA_STRING_ALLOC(config_.applicationUri.c_str());

    UA_LocalizedText_clear(
        &serverConfig->applicationDescription.applicationName);
    serverConfig->applicationDescription.applicationName =
        UA_LOCALIZEDTEXT_ALLOC("en-US", config_.applicationName.c_str());

    rc = UA_Server_run_startup(impl_->server);
    if (rc != UA_STATUSCODE_GOOD) {
        UA_Server_delete(impl_->server);
        impl_->server = nullptr;
        throw std::runtime_error(
            std::string("Open62541Server: run_startup failed: ") +
            UA_StatusCode_name(rc));
    }

    running_.store(true, std::memory_order_release);
    thread_ = std::jthread([this]() { runIterateLoop(); });

    logger_.info("OPC-UA server listening on opc.tcp://localhost:{}{}",
                 boundPort(), config_.endpointPath);
}

void Open62541Server::stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;

    // jthread join via destructor is fine, but we want explicit join
    // before UA_Server_delete to avoid the iterate loop racing with
    // teardown. Request stop, then join.
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }

    if (impl_->server != nullptr) {
        UA_Server_run_shutdown(impl_->server);
        UA_Server_delete(impl_->server);
        impl_->server = nullptr;
    }

    logger_.info("OPC-UA server stopped");
}

bool Open62541Server::isRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

std::size_t Open62541Server::connectedSessions() const noexcept {
    if (!isRunning() || impl_->server == nullptr) return 0;
    // open62541 v1.5 doesn't expose a stable session-count API; the
    // statistics struct is internal. For the dashboard we approximate
    // with the channel count, which is exposed.
    UA_ServerStatistics stats = UA_Server_getStatistics(impl_->server);
    return static_cast<std::size_t>(stats.scs.currentChannelCount);
}

std::uint16_t Open62541Server::boundPort() const noexcept {
    // When configured port is non-zero we trust the config; ephemeral
    // (port 0) is supported by open62541 internally but exposing the
    // OS-picked port requires walking the network manager's config,
    // which is API-private in v1.5. For tests we always pass an
    // explicit port.
    return isRunning() ? config_.port : 0;
}

bool Open62541Server::writeFloat(std::string_view path,
                                 float value) noexcept {
    return writeScalar(impl_->server, path, UA_TYPES[UA_TYPES_FLOAT], value);
}

bool Open62541Server::writeInt32(std::string_view path,
                                 std::int32_t value) noexcept {
    return writeScalar(impl_->server, path, UA_TYPES[UA_TYPES_INT32], value);
}

bool Open62541Server::writeBool(std::string_view path,
                                bool value) noexcept {
    // UA_Boolean is a typedef for unsigned char; pass through the
    // matching storage to satisfy the variant API.
    UA_Boolean boolValue = value ? UA_TRUE : UA_FALSE;
    return writeScalar(impl_->server, path,
                       UA_TYPES[UA_TYPES_BOOLEAN], boolValue);
}

bool Open62541Server::writeString(std::string_view path,
                                  std::string_view value) noexcept {
    if (impl_->server == nullptr) return false;
    UA_NodeId nodeId = resolveNode(impl_->server, path);
    if (UA_NodeId_isNull(&nodeId)) return false;

    // UA_String is a length+pointer pair; we don't need to allocate
    // because UA_Server_writeValue copies before queueing.
    std::string copy(value);
    UA_String s;
    s.length = copy.size();
    s.data = reinterpret_cast<UA_Byte*>(copy.data());

    UA_Variant var;
    UA_Variant_init(&var);
    UA_Variant_setScalar(&var, &s, &UA_TYPES[UA_TYPES_STRING]);
    const UA_StatusCode rc =
        UA_Server_writeValue(impl_->server, nodeId, var);
    UA_NodeId_clear(&nodeId);
    return rc == UA_STATUSCODE_GOOD;
}

void Open62541Server::runIterateLoop() noexcept {
    while (running_.load(std::memory_order_acquire)) {
        // run_iterate returns a sleep hint in ms but we cap at the
        // configured polling timeout to keep stop() latency bounded.
        UA_Server_run_iterate(impl_->server, /*waitInternal=*/true);
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kIterateTimeoutMs));
    }
}

}  // namespace app::integration::opcua
