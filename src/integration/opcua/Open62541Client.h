#pragma once

#include "src/integration/opcua/OpcUaClient.h"
#include "src/integration/IntegrationBackend.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace app::core { class Logger; }

namespace app::integration::opcua {

/// Concrete `OpcUaClient` backed by the open62541 v1.5.x C stack.
///
/// Owns a `UA_Client*` via pimpl so the header stays free of the C
/// stack's includes -- callers (and tests) can build against the
/// abstract `OpcUaClient` without pulling open62541 into their
/// translation units.
///
/// Lifecycle:
///   * `start()` dials `endpointUrl`, drives the connect handshake to
///     a SecureChannel + Session, creates a single subscription, then
///     spins a worker thread that loops `UA_Client_run_iterate` to
///     drain inbound publish responses. Throws on connect failure.
///   * Pre-start subscribe() calls are queued and replayed once the
///     session is up, mirroring `MqttClient::subscribe`.
///   * `stop()` cancels the subscription, disconnects, joins the
///     worker, frees the client. Idempotent and noexcept; safe from
///     destructors.
///
/// Threading: all open62541 calls (incl. `UA_Client_run_iterate` and
/// the monitored-item creation triggered by `subscribe`) are posted
/// to the worker thread so a single thread owns the `UA_Client*`
/// throughout its lifetime. Callbacks fire on the worker thread;
/// callers must marshal or lock if they touch shared state.
class Open62541Client final : public OpcUaClient {
public:
    struct Config {
        /// Endpoint URL the client dials. Loopback default mirrors
        /// `kDefaultOpcUaEndpointUrl` so a local demo "just works".
        std::string endpointUrl{"opc.tcp://127.0.0.1:4840"};
        /// Application identity advertised on the wire.
        std::string applicationUri{"urn:industrial-hmi:client"};
        std::string applicationName{"Industrial HMI Client"};
        /// How long `start()` waits for the connect handshake before
        /// giving up. open62541 itself enforces its own internal
        /// timeouts; this is a belt-and-suspenders guard so a stalled
        /// connect doesn't block the entire app boot.
        std::chrono::seconds connectTimeout{10};
        /// Iterate cadence on the worker thread. Lower = faster
        /// notification latency, higher = less CPU. 50 ms is a typical
        /// industrial sweet spot.
        std::chrono::milliseconds iterateInterval{50};
        /// Publishing interval requested when we create the
        /// subscription on the server (milliseconds). Server may
        /// negotiate down.
        double publishingIntervalMs{200.0};
    };

    /// Tag for the typed callback the open62541 data-change handler
    /// must dispatch to. Public so the C-linkage callback in
    /// Open62541Client.cpp can see it; behaves as an implementation
    /// detail otherwise.
    enum class ValueType : std::uint8_t { Float, Int32, Bool };

    /// Borrowed view of one subscription's typed callbacks. Owned by
    /// `subscriptions_`; the C callback receives a pointer to one of
    /// these as `monContext`. Public for the same reason as ValueType.
    struct PendingSubscription {
        std::string   nodeBrowsePath;
        ValueType     type{ValueType::Float};
        FloatCallback floatCb;
        Int32Callback int32Cb;
        BoolCallback  boolCb;
    };

    Open62541Client(Config config, core::Logger& logger);
    ~Open62541Client() override;

    Open62541Client(const Open62541Client&)            = delete;
    Open62541Client& operator=(const Open62541Client&) = delete;
    Open62541Client(Open62541Client&&)                 = delete;
    Open62541Client& operator=(Open62541Client&&)      = delete;

    // IntegrationBackend
    void start() override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] std::string name() const override { return "OPC-UA Client"; }
    [[nodiscard]] std::string metricsSummary() const override;
    [[nodiscard]] integration::BackendState
        connectionState() const noexcept override;

    // OpcUaClient
    [[nodiscard]] bool subscribeFloat(std::string_view nodeBrowsePath,
                                      FloatCallback callback) override;
    [[nodiscard]] bool subscribeInt32(std::string_view nodeBrowsePath,
                                      Int32Callback callback) override;
    [[nodiscard]] bool subscribeBool(std::string_view nodeBrowsePath,
                                     BoolCallback callback) override;
    [[nodiscard]] std::size_t monitoredItemCount() const noexcept override;
    [[nodiscard]] std::string endpointUrl() const override {
        return config_.endpointUrl;
    }

private:
    /// Idempotent stop body. Called by both the public stop() and the
    /// destructor; the destructor can't call a virtual override.
    void stopImpl() noexcept;

    /// Worker-thread body. Drives UA_Client_run_iterate at the
    /// configured cadence until the stop token fires.
    void runIoLoop(std::stop_token stop);

    /// Open the SecureChannel + Session against `config_.endpointUrl`.
    /// Called on the worker thread under the impl mutex.
    /// Throws std::runtime_error on any failure.
    void connectSync();

    /// Create a single subscription on the server (one is enough for
    /// the monitored-items count we care about). Stores the
    /// subscription id in `impl_`.
    void createSubscription();

    /// Replay every queued subscribeXxx() onto the live session.
    /// Called once `connectSync` succeeds.
    void replaySubscriptions();

    /// Internal subscribe entry point shared by the three typed
    /// public overloads. Stores the request, fires the monitored-item
    /// create immediately if we're already running.
    bool registerSubscription(PendingSubscription pending);

    /// Send `MonitoredItems_createDataChange` for one pending entry
    /// on the worker thread. Returns false if the node couldn't be
    /// resolved or the create was rejected. Increments
    /// `monitoredItemCount_` on success.
    bool armMonitoredItem(const PendingSubscription& pending);

    Config         config_;
    core::Logger&  logger_;
    std::atomic<bool>          running_{false};
    std::atomic<std::size_t>   monitoredItemCount_{0};

    /// Pending + already-armed subscriptions. Guarded so subscribe()
    /// is safe from any thread; the worker thread snapshots the
    /// vector under the lock when arming.
    ///
    /// Stored as `unique_ptr` so the per-entry address is stable
    /// across vector growth -- open62541's C callback receives a
    /// `void* monContext` that we set to `pending.get()`, and a
    /// later subscribe() must not invalidate already-armed pointers.
    mutable std::mutex subscriptionsMutex_;
    std::vector<std::unique_ptr<PendingSubscription>> subscriptions_;

    /// Pimpl holder for `UA_Client*` + the open62541 subscription id.
    /// Defined in the .cpp so the header doesn't pull `<open62541/...>`
    /// into every TU that includes us.
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::jthread          thread_;
};

}  // namespace app::integration::opcua
