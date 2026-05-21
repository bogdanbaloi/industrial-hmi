#include "src/integration/MasterToSlaveBridge.h"

#include <ctime>
#include <sstream>
#include <string>

namespace app::integration {

namespace {
// Bridge name surfaced to logs + the BackendHealthBar. Kept ASCII
// (no arrow glyph) so it renders identically across all platforms
// and fonts; the visual treatment is the sidebar's job.
constexpr const char* kBridgeName = "Master->Slave";
}  // namespace

MasterToSlaveBridge::MasterToSlaveBridge(model::ProductionModel& master,
                                         model::ProductionModel& slave) noexcept
    : master_(master), slave_(slave) {}

void MasterToSlaveBridge::start() {
    // Subscribe exactly once across the bridge's lifetime.
    // ProductionModel intentionally has no remove-callback path; we
    // gate the forward on `running_` instead. See the class docstring
    // and ADR-0011 for the reasoning.
    bool expected = false;
    if (subscribed_.compare_exchange_strong(expected, true)) {
        master_.onEquipmentStatusChanged(
            [this](const model::EquipmentStatus& status) {
                onMasterEquipmentEvent(status);
            });
    }
    running_.store(true, std::memory_order_release);
}

void MasterToSlaveBridge::stop() {
    // Mute the forward. A callback already in flight past the
    // running-check will still complete -- that's a single setter
    // call, harmless. New callbacks see running_=false and bail.
    running_.store(false, std::memory_order_release);
}

bool MasterToSlaveBridge::isRunning() const {
    return running_.load(std::memory_order_acquire);
}

std::string MasterToSlaveBridge::name() const {
    return kBridgeName;
}

BackendState MasterToSlaveBridge::connectionState() const noexcept {
    // The bridge has no external dependency that can be "Connecting"
    // or "Degraded" -- it's either wired to the master callback
    // (Connected) or muted (Disconnected). Mapping cleanly into the
    // dashboard's coloured-dot vocabulary.
    return isRunning() ? BackendState::Connected
                       : BackendState::Disconnected;
}

std::string MasterToSlaveBridge::metricsSummary() const {
    std::size_t count;
    std::chrono::system_clock::time_point last;
    {
        const std::lock_guard<std::mutex> lock(metricsMutex_);
        count = forwarded_.load(std::memory_order_acquire);
        last  = lastForward_;
    }

    std::ostringstream out;
    out << count << " forwarded";
    if (count > 0) {
        const auto tt = std::chrono::system_clock::to_time_t(last);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
        out << " (last " << buf << ")";
    }
    return out.str();
}

std::size_t MasterToSlaveBridge::forwardedCount() const noexcept {
    return forwarded_.load(std::memory_order_acquire);
}

void MasterToSlaveBridge::onMasterEquipmentEvent(
        const model::EquipmentStatus& status) {
    // Gate on running_ so a stop() flips us off cleanly without
    // needing to unsubscribe from the model (which has no remove API).
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    // Mirror the master's supply level onto the slave's same equipment
    // id. setEquipmentSupplyLevel is the ProductionModel-wide inbound
    // setter: every other ingest bridge (MQTT, Modbus, OPC-UA) calls
    // the same entry point, so the slave's behaviour under this bridge
    // is indistinguishable from a real cross-process master feeding
    // it via MQTT -- which is exactly what we'll swap this for in the
    // cross-process follow-up.
    slave_.setEquipmentSupplyLevel(status.equipmentId,
                                   status.supplyLevel);

    {
        const std::lock_guard<std::mutex> lock(metricsMutex_);
        lastForward_ = std::chrono::system_clock::now();
    }
    forwarded_.fetch_add(1, std::memory_order_acq_rel);
}

}  // namespace app::integration
