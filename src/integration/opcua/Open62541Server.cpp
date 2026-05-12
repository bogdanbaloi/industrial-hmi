#include "src/integration/opcua/Open62541Server.h"

#include "src/core/LoggerBase.h"
#include "src/integration/opcua/OpcUaCommandSink.h"

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
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
    // setScalarCopy takes a const void* and copies into the variant,
    // avoiding the const_cast trap that the non-copy setScalar would
    // require. UA_Server_writeValue then takes the variant by value
    // and serialises it, so the copy is unavoidable anyway.
    UA_Variant_setScalarCopy(&var, &value, &type);

    const UA_StatusCode rc = UA_Server_writeValue(server, nodeId, var);
    UA_Variant_clear(&var);
    UA_NodeId_clear(&nodeId);
    return rc == UA_STATUSCODE_GOOD;
}

}  // namespace

/// Per-node context the C-linkage open62541 callbacks receive in
/// their `methodContext` / `nodeContext` slot. Owns the sink pointer
/// + the command name / variable path so the callback can dispatch
/// without consulting any global registry. Lifetime: pinned by the
/// vector below so the address open62541 stores stays stable.
struct Open62541ServerCallbackContext {
    OpcUaCommandSink* sink     = nullptr;
    std::string       commandName;   // for method callbacks
    std::string       nodePath;      // for write callbacks
};

struct Open62541Server::Impl {
    UA_Server* server = nullptr;

    /// Owning storage for the per-callback contexts. open62541
    /// remembers a `void*` per node; if our vector reallocated
    /// underneath, the pointer it stores would dangle. We use
    /// `unique_ptr` so the entry address is stable across pushes.
    std::vector<std::unique_ptr<Open62541ServerCallbackContext>>
        callbackContexts;
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
    UA_Boolean boolValue = value;
    return writeScalar(impl_->server, path,
                       UA_TYPES[UA_TYPES_BOOLEAN], boolValue);
}

bool Open62541Server::writeString(std::string_view path,
                                  std::string_view value) noexcept {
    if (impl_->server == nullptr) return false;
    UA_NodeId nodeId = resolveNode(impl_->server, path);
    if (UA_NodeId_isNull(&nodeId)) return false;

    // UA_String_fromChars copies into a fresh allocation owned by the
    // variant; no reinterpret_cast or const-stripping needed. The
    // matching UA_String_clear runs as part of UA_Variant_clear below.
    const std::string copy(value);
    UA_String s = UA_String_fromChars(copy.c_str());

    UA_Variant var;
    UA_Variant_init(&var);
    UA_Variant_setScalarCopy(&var, &s, &UA_TYPES[UA_TYPES_STRING]);
    const UA_StatusCode rc =
        UA_Server_writeValue(impl_->server, nodeId, var);
    UA_String_clear(&s);
    UA_Variant_clear(&var);
    UA_NodeId_clear(&nodeId);
    return rc == UA_STATUSCODE_GOOD;
}

namespace {

/// Split "Factory/Lines/Line0" -> ("Factory/Lines", "Line0"). Returns
/// empty parent for top-level (parent is Objects folder).
std::pair<std::string, std::string>
splitParentLeaf(std::string_view browsePath) {
    const auto pos = browsePath.rfind('/');
    if (pos == std::string_view::npos) {
        return {std::string{}, std::string{browsePath}};
    }
    return {std::string{browsePath.substr(0, pos)},
            std::string{browsePath.substr(pos + 1)}};
}

/// Resolve a parent path to a NodeId, defaulting to Objects folder
/// when the path is empty (top-level child of Objects/).
UA_NodeId resolveParent(UA_Server* server, std::string_view parentPath) {
    if (parentPath.empty()) {
        return UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    }
    return resolveNode(server, parentPath);
}

}  // namespace

bool Open62541Server::addObject(std::string_view browsePath) {
    if (impl_->server == nullptr) return false;

    auto [parent, leaf] = splitParentLeaf(browsePath);
    UA_NodeId parentId = resolveParent(impl_->server, parent);
    if (UA_NodeId_isNull(&parentId)) return false;

    UA_ObjectAttributes attr = UA_ObjectAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", leaf.c_str());

    const UA_StatusCode rc = UA_Server_addObjectNode(
        impl_->server,
        UA_NODEID_NULL,                 // server picks the NodeId
        parentId,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(1, leaf.data()),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
        attr,
        nullptr,
        nullptr);

    UA_LocalizedText_clear(&attr.displayName);
    if (!parent.empty()) UA_NodeId_clear(&parentId);

    return rc == UA_STATUSCODE_GOOD;
}

namespace {

template <typename T>
bool addVariableImpl(UA_Server* server,
                     std::string_view browsePath,
                     const UA_DataType& type,
                     const T& initial) {
    if (server == nullptr) return false;

    auto [parent, leaf] = splitParentLeaf(browsePath);
    UA_NodeId parentId = resolveParent(server, parent);
    if (UA_NodeId_isNull(&parentId)) return false;

    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", leaf.c_str());
    attr.dataType = type.typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    UA_Variant_setScalarCopy(&attr.value, &initial, &type);

    const UA_StatusCode rc = UA_Server_addVariableNode(
        server,
        UA_NODEID_NULL,
        parentId,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(1, leaf.data()),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        attr,
        nullptr,
        nullptr);

    UA_LocalizedText_clear(&attr.displayName);
    UA_Variant_clear(&attr.value);
    if (!parent.empty()) UA_NodeId_clear(&parentId);

    return rc == UA_STATUSCODE_GOOD;
}

}  // namespace

bool Open62541Server::addFloatVariable(std::string_view path,
                                       float initial) {
    return addVariableImpl(impl_->server, path,
                           UA_TYPES[UA_TYPES_FLOAT], initial);
}

bool Open62541Server::addInt32Variable(std::string_view path,
                                       std::int32_t initial) {
    return addVariableImpl(impl_->server, path,
                           UA_TYPES[UA_TYPES_INT32], initial);
}

bool Open62541Server::addBoolVariable(std::string_view path,
                                      bool initial) {
    UA_Boolean v = initial;
    return addVariableImpl(impl_->server, path,
                           UA_TYPES[UA_TYPES_BOOLEAN], v);
}

bool Open62541Server::addStringVariable(std::string_view path,
                                        std::string_view initial) {
    if (impl_->server == nullptr) return false;

    auto [parent, leaf] = splitParentLeaf(path);
    UA_NodeId parentId = resolveParent(impl_->server, parent);
    if (UA_NodeId_isNull(&parentId)) return false;

    const std::string copy(initial);
    UA_String s = UA_String_fromChars(copy.c_str());

    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", leaf.c_str());
    attr.dataType = UA_TYPES[UA_TYPES_STRING].typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    UA_Variant_setScalarCopy(&attr.value, &s, &UA_TYPES[UA_TYPES_STRING]);

    const UA_StatusCode rc = UA_Server_addVariableNode(
        impl_->server,
        UA_NODEID_NULL,
        parentId,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(1, leaf.data()),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        attr,
        nullptr,
        nullptr);

    UA_LocalizedText_clear(&attr.displayName);
    UA_Variant_clear(&attr.value);
    UA_String_clear(&s);
    if (!parent.empty()) UA_NodeId_clear(&parentId);

    return rc == UA_STATUSCODE_GOOD;
}

namespace {

/// open62541 method-callback shim. The `methodContext` we registered
/// at addMethod-time is a `Open62541ServerCallbackContext*`; dispatch
/// the parameterless command to its sink. open62541's signature is
/// fixed by the spec, so naming + parameter count follow the C
/// convention -- the readability lints don't apply.
//
// NOLINTNEXTLINE(readability-named-parameter,bugprone-easily-swappable-parameters,readability-function-size)
UA_StatusCode methodCallbackShim(UA_Server* /*server*/,
                                 const UA_NodeId* /*sessionId*/,
                                 void* /*sessionContext*/,
                                 const UA_NodeId* /*methodId*/,
                                 void* methodContext,
                                 const UA_NodeId* /*objectId*/,
                                 void* /*objectContext*/,
                                 size_t /*inputSize*/,
                                 const UA_Variant* /*input*/,
                                 size_t /*outputSize*/,
                                 UA_Variant* /*output*/) {
    if (methodContext == nullptr) return UA_STATUSCODE_BADINTERNALERROR;
    auto* ctx = static_cast<Open62541ServerCallbackContext*>(methodContext);
    if (ctx->sink == nullptr) return UA_STATUSCODE_BADINTERNALERROR;
    ctx->sink->onCommand(ctx->commandName);
    return UA_STATUSCODE_GOOD;
}

/// open62541 value-callback shim for writable Bool variables.
/// Decodes the freshly-written value out of the data-value variant
/// and hands it to `sink.onBoolWrite(path, value)`.
//
// NOLINTNEXTLINE(readability-named-parameter,bugprone-easily-swappable-parameters)
void boolWriteCallbackShim(UA_Server* /*server*/,
                           const UA_NodeId* /*sessionId*/,
                           void* /*sessionContext*/,
                           const UA_NodeId* /*nodeId*/,
                           void* nodeContext,
                           const UA_NumericRange* /*range*/,
                           const UA_DataValue* data) {
    if (nodeContext == nullptr || data == nullptr) return;
    if (!data->hasValue) return;
    if (!UA_Variant_hasScalarType(&data->value,
                                  &UA_TYPES[UA_TYPES_BOOLEAN])) return;
    auto* ctx = static_cast<Open62541ServerCallbackContext*>(nodeContext);
    if (ctx->sink == nullptr) return;
    const auto v = *static_cast<UA_Boolean*>(data->value.data);
    ctx->sink->onBoolWrite(ctx->nodePath, v != 0);
}

}  // namespace

bool Open62541Server::addMethod(std::string_view browsePath,
                                 OpcUaCommandSink& sink) {
    if (impl_->server == nullptr) return false;
    auto [parent, leaf] = splitParentLeaf(browsePath);
    UA_NodeId parentId = resolveParent(impl_->server, parent);
    if (UA_NodeId_isNull(&parentId)) return false;

    // Context owns the strings the callback will read. Stable
    // address because the vector stores unique_ptrs.
    auto ctx = std::make_unique<Open62541ServerCallbackContext>();
    ctx->sink        = &sink;
    ctx->commandName = leaf;
    auto* contextPtr = ctx.get();
    impl_->callbackContexts.push_back(std::move(ctx));

    UA_MethodAttributes attr = UA_MethodAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", leaf.c_str());
    attr.executable     = true;
    attr.userExecutable = true;

    const UA_StatusCode rc = UA_Server_addMethodNode(
        impl_->server,
        UA_NODEID_NULL,
        parentId,
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
        UA_QUALIFIEDNAME(1, leaf.data()),
        attr,
        methodCallbackShim,
        /*inputArgumentsSize=*/0,  nullptr,
        /*outputArgumentsSize=*/0, nullptr,
        /*nodeContext=*/contextPtr,
        /*outNewNodeId=*/nullptr);

    UA_LocalizedText_clear(&attr.displayName);
    if (!parent.empty()) UA_NodeId_clear(&parentId);
    return rc == UA_STATUSCODE_GOOD;
}

bool Open62541Server::addBoolVariableWithWriteCallback(
        std::string_view browsePath, bool initial,
        OpcUaCommandSink& sink) {
    if (impl_->server == nullptr) return false;

    // Add the variable inline with BOTH accessLevel and userAccessLevel
    // set to READ|WRITE. `UA_VariableAttributes_default` leaves
    // userAccessLevel at 0 and open62541 short-circuits external
    // client writes with BadWriteNotSupported in that case; the
    // standard `addBoolVariable` path that other Factory state nodes
    // use is correct for read-only telemetry, but writable surfaces
    // need both bits explicitly.
    auto [parent, leaf] = splitParentLeaf(browsePath);
    UA_NodeId parentId = resolveParent(impl_->server, parent);
    if (UA_NodeId_isNull(&parentId)) return false;

    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", leaf.c_str());
    attr.dataType    = UA_TYPES[UA_TYPES_BOOLEAN].typeId;
    // Open the variable to every write flavour open62541 understands.
    // Just READ|WRITE bits aren't enough on this version: client
    // libraries (asyncua, paho-opcua) that ship a DataValue with
    // ServerTimestamp / StatusCode set get BadWriteNotSupported even
    // when they pass empty values, because open62541 still inspects
    // the wire bits. Granting STATUSWRITE + TIMESTAMPWRITE on top of
    // READ|WRITE makes every flavour go through cleanly.
    constexpr UA_Byte kFullWriteAccess =
        UA_ACCESSLEVELMASK_READ |
        UA_ACCESSLEVELMASK_WRITE |
        UA_ACCESSLEVELMASK_STATUSWRITE |
        UA_ACCESSLEVELMASK_TIMESTAMPWRITE;
    attr.accessLevel     = kFullWriteAccess;
    attr.userAccessLevel = kFullWriteAccess;
    UA_Boolean v = initial;
    UA_Variant_setScalarCopy(&attr.value, &v,
                              &UA_TYPES[UA_TYPES_BOOLEAN]);

    auto ctx = std::make_unique<Open62541ServerCallbackContext>();
    ctx->sink     = &sink;
    ctx->nodePath = std::string(browsePath);
    auto* contextPtr = ctx.get();
    impl_->callbackContexts.push_back(std::move(ctx));

    UA_NodeId outNodeId = UA_NODEID_NULL;
    UA_StatusCode rc = UA_Server_addVariableNode(
        impl_->server,
        UA_NODEID_NULL,
        parentId,
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(1, leaf.data()),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        attr,
        /*nodeContext=*/contextPtr,
        &outNodeId);

    UA_LocalizedText_clear(&attr.displayName);
    UA_Variant_clear(&attr.value);
    if (!parent.empty()) UA_NodeId_clear(&parentId);

    if (rc == UA_STATUSCODE_GOOD) {
        UA_ValueCallback cb;
        cb.onRead  = nullptr;
        cb.onWrite = boolWriteCallbackShim;
        rc = UA_Server_setVariableNode_valueCallback(
            impl_->server, outNodeId, cb);
    }
    UA_NodeId_clear(&outNodeId);
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
