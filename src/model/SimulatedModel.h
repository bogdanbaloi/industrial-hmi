#pragma once

#include "src/model/ProductionModel.h"
#include "src/model/ProductionTypes.h"
#include "src/model/SystemStateMachine.h"
#include "src/model/ThroughputMeter.h"
#include "src/core/LoggerBase.h"

#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <algorithm>
#include <unordered_set>

namespace app::model {

/// Simulated Model layer for demo purposes
///
/// This stub provides simulated data and state changes to demonstrate
/// the MVP architecture without exposing proprietary business logic.
///
/// Implements ProductionModel so DashboardPresenter can depend on the
/// abstraction; tests inject a MockProductionModel instead of this
/// singleton.
class SimulatedModel : public ProductionModel {
public:
    // Simulation parameters (not business logic -- just demo noise shaping)
    static constexpr std::size_t kEquipmentCount = 4;
    static constexpr float kQualityRateJitter = 0.3f;
    static constexpr int kMinUnitsPerTick = 1;
    static constexpr int kMaxUnitsPerTick = 3;
    static constexpr float kQualityRateMin = 85.0f;
    static constexpr float kQualityRateMax = 100.0f;
    // Domain of the supplyLevel field per ProductionTypes.h (percent).
    // Mirrors the natural clamp range used by the existing UI bars.
    static constexpr int kSupplyLevelMin = 0;
    static constexpr int kSupplyLevelMax = 100;
    static constexpr int kEquipmentStatusProcessing = 2;
    static constexpr int kEquipmentStatusOnline = 1;
    static constexpr int kEquipmentStatusOffline = 0;
    static constexpr std::uint_fast32_t kRngSeed = 42;
    /// Base for generated work-unit sequence numbers (see workUnitSequence_).
    static constexpr std::uint64_t kWorkUnitSeqBase = 2024000000ULL;

    static SimulatedModel& instance() {
        static SimulatedModel inst;
        return inst;
    }

    /// Optional logger (DI, same pattern as DatabaseManager::setLogger).
    /// Tests skip this call so SimulatedModel stays header-only-linkable
    /// without dragging Application into the test target.
    void setLogger(app::core::Logger& logger) {
        logger_ = &logger;
    }

    void onEquipmentStatusChanged(EquipmentCallback cb) override {
        const std::scoped_lock lock(mutex_);
        equipmentCallbacks_.push_back(cb);
    }

    void onActuatorStatusChanged(ActuatorCallback cb) override {
        const std::scoped_lock lock(mutex_);
        actuatorCallbacks_.push_back(cb);
    }

    void onQualityCheckpointChanged(QualityCheckpointCallback cb) override {
        const std::scoped_lock lock(mutex_);
        qualityCallbacks_.push_back(cb);
    }

    void onWorkUnitChanged(WorkUnitCallback cb) override {
        const std::scoped_lock lock(mutex_);
        workUnitCallbacks_.push_back(cb);
    }

    void onSystemStateChanged(StateCallback cb) override {
        const std::scoped_lock lock(mutex_);
        stateCallbacks_.push_back(cb);
    }

    /// Drop every subscription. Used by MainWindow during a language
    /// rebuild: after this returns, no callback references an old (about
    /// to be destroyed) presenter, so rebuilding the page + presenter
    /// graph does not leave dangling lambdas behind.
    void clearCallbacks() {
        const std::scoped_lock lock(mutex_);
        if (logger_) {
            logger_->debug(
                "Model: clearing callbacks (eq={}, act={}, qc={}, wu={}, st={})",
                equipmentCallbacks_.size(), actuatorCallbacks_.size(),
                qualityCallbacks_.size(), workUnitCallbacks_.size(),
                stateCallbacks_.size());
        }
        equipmentCallbacks_.clear();
        actuatorCallbacks_.clear();
        qualityCallbacks_.clear();
        workUnitCallbacks_.clear();
        stateCallbacks_.clear();
    }

    // User commands
    void startProduction() override {
        if (logger_) logger_->info("Model: dispatch Start (production started)");
        stateMachine_.start();           // SM observer fires notifyStateChange
        simulateProductionCycle();
    }

    void stopProduction() override {
        if (logger_) logger_->info("Model: dispatch Stop (production stopped)");
        stateMachine_.stop();
    }

    void resetSystem() override {
        if (logger_) logger_->info("Model: dispatch Reset (clearing work unit progress)");
        stateMachine_.reset();           // -> IDLE; SM observer notifies
        currentWorkUnit_.completedOperations = 0;
        // A reset is not a completion; drop the rate history so the card
        // zeroes immediately instead of decaying from a stale value.
        throughputMeter_.clear();
        currentWorkUnit_.throughputUnitsPerHour = 0.0;
        notifyWorkUnitChange();
    }

    void startCalibration() override {
        if (logger_) logger_->info("Model: dispatch Calibrate");
        stateMachine_.calibrate();
    }

    void setEquipmentEnabled(uint32_t equipmentId, bool enabled) override {
        if (equipmentId >= kEquipmentCount) {
            if (logger_) {
                logger_->warn("Model: setEquipmentEnabled out of range (id={}, max={})",
                              equipmentId, kEquipmentCount);
            }
            return;
        }
        if (logger_) {
            logger_->debug("Model: equipment {} -> {}",
                           equipmentId, enabled ? "online" : "offline");
        }
        equipmentStatuses_[equipmentId].status =
            enabled ? kEquipmentStatusOnline : kEquipmentStatusOffline;
        notifyEquipmentChange(equipmentId);
    }

    /// External (ingest-bridge) supply-level update. Out-of-range ids
    /// are dropped silently. Value is clamped to the field's natural
    /// 0..100 percent domain. The simulator never overwrites
    /// supplyLevel autonomously (it's only set in initializeDemoData
    /// today), so there's no override flag to maintain here -- the
    /// last writer wins, and that's the bridge.
    void setEquipmentSupplyLevel(uint32_t equipmentId,
                                 int level) override {
        if (equipmentId >= kEquipmentCount) {
            if (logger_) {
                logger_->warn("Model: setEquipmentSupplyLevel out of range "
                              "(id={}, max={})", equipmentId, kEquipmentCount);
            }
            return;
        }
        const int clamped = std::clamp(level,
                                       kSupplyLevelMin, kSupplyLevelMax);
        {
            const std::scoped_lock lock(mutex_);
            equipmentStatuses_[equipmentId].supplyLevel = clamped;
        }
        if (logger_) {
            logger_->trace("Model: equipment {} supply -> {}",
                           equipmentId, clamped);
        }
        notifyEquipmentChange(equipmentId);
    }

    /// External (ingest-bridge) quality pass-rate update. Out-of-
    /// range ids dropped silently; value clamped to 0..100. Marks
    /// the checkpoint as externally driven so tickSimulation's
    /// random walk stops touching this field -- otherwise the next
    /// tick would clobber the fresh reading. The bridge becomes the
    /// authoritative source until process restart.
    void setQualityPassRate(uint32_t checkpointId, float rate) override {
        if (!qualityCheckpoints_.count(checkpointId)) {
            // Distinguish "model not yet initialised" (expected
            // startup race -- backends begin polling before
            // initializeDemoData() runs; the first few inbound
            // values legitimately land on an empty model) from
            // "real misconfig" (a deployment whose register map
            // points at a checkpoint id beyond what the operator
            // configured). Empty map = startup race, drop silently.
            // Non-empty map + unknown id = warn so the operator
            // catches the mistake.
            if (logger_ && !qualityCheckpoints_.empty()) {
                logger_->warn("Model: setQualityPassRate unknown id {}",
                              checkpointId);
            }
            return;
        }
        const float clamped = std::clamp(rate,
                                         kQualityRateMin, kQualityRateMax);
        {
            const std::scoped_lock lock(mutex_);
            qualityCheckpoints_[checkpointId].passRate = clamped;
            externalPassRateOverrides_.insert(checkpointId);
        }
        if (logger_) {
            logger_->trace("Model: quality {} passRate -> {:.1f}",
                           checkpointId, clamped);
        }
        notifyQualityChange(checkpointId);
    }

    /// Load a product's recipe onto the line as the active work unit.
    /// Mutates work-unit identity + per-checkpoint targets under the
    /// lock; notifies observers outside it (matching the notify*
    /// pattern). Only touches passRateTarget, not passRate, so an
    /// external ingest bridge keeps ownership of the live pass-rate.
    void loadProduct(const Product& product, const Recipe& recipe) override {
        {
            const std::scoped_lock lock(mutex_);

            ++workUnitSequence_;
            currentWorkUnit_.workUnitId =
                "WU-" + std::to_string(workUnitSequence_);
            currentWorkUnit_.productId = product.productCode;
            currentWorkUnit_.description =
                "Batch " + currentWorkUnit_.workUnitId + " | " + product.name;
            currentWorkUnit_.totalOperations = recipe.totalOperations;
            currentWorkUnit_.completedOperations = 0;  // fresh batch
            // New batch: the previous product's completion history no
            // longer describes this one, so start the rate measurement
            // clean (it climbs again as this batch completes units).
            throughputMeter_.clear();
            currentWorkUnit_.throughputUnitsPerHour = 0.0;

            // Fresh batch: zero inspection counters and apply each
            // recipe target onto the matching checkpoint (by name, not
            // position). Checkpoints without a recipe target keep their
            // current target.
            for (auto& [id, cp] : qualityCheckpoints_) {
                cp.unitsInspected = 0;
                cp.defectsFound = 0;
                for (const auto& target : recipe.checkpointTargets) {
                    if (target.checkpointName == cp.name) {
                        cp.passRateTarget = target.passRateTarget;
                        break;
                    }
                }
            }
        }

        if (logger_) {
            logger_->info("Model: loaded product {} -> {} ({} ops, {} targets)",
                          product.productCode, currentWorkUnit_.workUnitId,
                          recipe.totalOperations,
                          recipe.checkpointTargets.size());
        }

        notifyWorkUnitChange();
        for (const auto& [id, cp] : qualityCheckpoints_) {
            notifyQualityChange(id);
        }
    }

    [[nodiscard]] SystemState getState() const override { return stateMachine_.state(); }

    [[nodiscard]] std::string lastFaultReason() const override {
        return stateMachine_.lastFaultReason();
    }

    /// Concrete (non-virtual) trigger for the safe-state path. Wired to a
    /// console command + (future) a GUI control so the demo can exercise
    /// the Fault -> ERROR lock-out flow without modifying the
    /// ProductionModel interface (faults originate inside the model in a
    /// real deployment; this knob is for the simulator only).
    void triggerFault(const std::string& reason) {
        if (logger_) logger_->warn("Model: dispatch Fault ({})", reason);
        stateMachine_.fault(reason);
    }

    [[nodiscard]] QualityCheckpoint getQualityCheckpoint(uint32_t id) const override {
        return qualityCheckpoints_.at(id);
    }

    [[nodiscard]] std::vector<QualityCheckpoint> getQualityCheckpoints() const override {
        const std::scoped_lock lock(mutex_);
        std::vector<QualityCheckpoint> out;
        out.reserve(qualityCheckpoints_.size());
        // std::map iterates in ascending key (id) order.
        for (const auto& [id, cp] : qualityCheckpoints_) {
            out.push_back(cp);
        }
        return out;
    }

    [[nodiscard]] WorkUnit getWorkUnit() const override { return currentWorkUnit_; }

    /// Advance simulation by one tick (called by auto refresh timer)
    void tickSimulation() {
        if (logger_) logger_->trace("Model: simulation tick");
        {
            const std::scoped_lock lock(mutex_);
            std::uniform_real_distribution<float> rateDist(-kQualityRateJitter, kQualityRateJitter);
            std::uniform_int_distribution<int> unitDist(kMinUnitsPerTick, kMaxUnitsPerTick);

            for (auto& [id, cp] : qualityCheckpoints_) {
                // Skip the random-walk on passRate when an external
                // ingest bridge has taken ownership of this field.
                // unitsInspected stays simulator-driven either way --
                // no inbound channel ships that field today.
                if (!externalPassRateOverrides_.count(id)) {
                    cp.passRate = std::clamp(
                        cp.passRate + rateDist(rng_),
                        kQualityRateMin, kQualityRateMax);
                }
                cp.unitsInspected += unitDist(rng_);
            }

            const auto now = ThroughputMeter::Clock::now();
            if (currentWorkUnit_.completedOperations < currentWorkUnit_.totalOperations) {
                currentWorkUnit_.completedOperations++;
            } else {
                // Genuine production rollover: the unit finished its last
                // operation, so a fresh one starts. This is the only place
                // a completion is counted -- reset/loadProduct also zero
                // completedOperations but clear the meter explicitly, so
                // they are never mistaken for throughput.
                currentWorkUnit_.completedOperations = 0;
                throughputMeter_.recordCompletion(now);
            }
            // Recompute every tick (not just on completion) so the rate
            // decays toward 0 when the line stalls between completions.
            currentWorkUnit_.throughputUnitsPerHour =
                throughputMeter_.unitsPerHour(now);
        }

        for (const auto& [id, cp] : qualityCheckpoints_) {
            notifyQualityChange(id);
        }
        notifyWorkUnitChange();
    }

    void notifyQualityChange(uint32_t id) {
        std::vector<QualityCheckpointCallback> cbs;
        QualityCheckpoint cp;
        {
            const std::scoped_lock lock(mutex_);
            cbs = qualityCallbacks_;
            cp = qualityCheckpoints_[id];
        }
        for (auto& cb : cbs) { cb(cp); }
    }

    void notifyWorkUnitChange() {
        std::vector<WorkUnitCallback> cbs;
        WorkUnit wu;
        {
            const std::scoped_lock lock(mutex_);
            cbs = workUnitCallbacks_;
            wu = currentWorkUnit_;
        }
        for (auto& cb : cbs) { cb(wu); }
    }
    
    // Initialize with demo data
    // Demo-data literals (batch IDs, counts, pass rates, etc.) are data,
    // not behavior -- suppress magic-number lint for this block.
    // NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    void initializeDemoData() {
        if (logger_) {
            logger_->info("Model: initializing demo data "
                          "(3 equipment lines, 3 quality checkpoints)");
        }
        // Equipment statuses (3 lines: A-LINE, B-LINE, C-LINE)
        equipmentStatuses_[0] = {0, 2, 85, "85K tablets/hr"};  // A-LINE (Processing)
        equipmentStatuses_[1] = {1, 1, 95, "Film coating"};     // B-LINE (Online)
        equipmentStatuses_[2] = {2, 0, 0, "Standby"};           // C-LINE (Offline)
        
        // Actuator statuses
        actuatorStatuses_[0] = {0, 1, 150, 200, true, false};  // Working
        actuatorStatuses_[1] = {1, 0, 0, 0, true, true};  // Idle at home
        
        // Quality checkpoints (3 checks)
        qualityCheckpoints_[0] = {0, "Weight Check", 0, 645, 12, 98.1f, "Underweight"};
        qualityCheckpoints_[1] = {1, "Hardness Test", 0, 645, 28, 95.7f, "Soft tablet (65N)"};
        qualityCheckpoints_[2] = {2, "Final Inspection", 1, 645, 45, 93.0f, "Coating uneven"};
        
        // Work unit (Pharma batch)
        currentWorkUnit_ = {
            "WU-2024-001234",
            "TAB-200",
            "Batch WU-2024-001234 | TAB-200",
            3,
            5
        };
        
        // Drive the SM to IDLE for a clean fixture (no-op when fresh).
        stateMachine_.reset();

        // Notify initial state
        for (const auto& [id, status] : equipmentStatuses_) {
            notifyEquipmentChange(id);
        }
        for (const auto& [id, status] : actuatorStatuses_) {
            notifyActuatorChange(id);
        }
        for (const auto& [id, checkpoint] : qualityCheckpoints_) {
            notifyQualityChange(id);
        }
        notifyWorkUnitChange();
        notifyStateChange();
    }
    // NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

    SimulatedModel(const SimulatedModel&) = delete;
    SimulatedModel& operator=(const SimulatedModel&) = delete;
    SimulatedModel(SimulatedModel&&) = delete;
    SimulatedModel& operator=(SimulatedModel&&) = delete;

private:
    SimulatedModel() {
        // The formal state machine fires notifyStateChange() automatically
        // on every real transition, so the command bodies below stay free
        // of explicit notify calls and idempotent commands (e.g. `start`
        // from RUNNING) no longer produce phantom view refreshes.
        stateMachine_.onStateChanged(
            [this](SystemState) { notifyStateChange(); });
    }

    void notifyEquipmentChange(uint32_t equipmentId) {
        std::vector<EquipmentCallback> cbs;
        EquipmentStatus es;
        {
            const std::scoped_lock lock(mutex_);
            if (!equipmentStatuses_.count(equipmentId)) return;
            cbs = equipmentCallbacks_;
            es = equipmentStatuses_[equipmentId];
        }
        for (auto& cb : cbs) { cb(es); }
    }

    void notifyActuatorChange(uint32_t actuatorId) {
        std::vector<ActuatorCallback> cbs;
        ActuatorStatus as;
        {
            const std::scoped_lock lock(mutex_);
            if (!actuatorStatuses_.count(actuatorId)) return;
            cbs = actuatorCallbacks_;
            as = actuatorStatuses_[actuatorId];
        }
        for (auto& cb : cbs) { cb(as); }
    }

    void notifyStateChange() {
        std::vector<StateCallback> cbs;
        {
            const std::scoped_lock lock(mutex_);
            cbs = stateCallbacks_;
        }
        const SystemState state = stateMachine_.state();
        for (auto& cb : cbs) { cb(state); }
    }
    
    void simulateProductionCycle() {
        // Simulate production progress
        if (currentWorkUnit_.completedOperations < currentWorkUnit_.totalOperations) {
            currentWorkUnit_.completedOperations++;
            notifyWorkUnitChange();

            // Update equipment to "processing"
            constexpr uint32_t kBLineId = 1;
            equipmentStatuses_[kBLineId].status = kEquipmentStatusProcessing;
            notifyEquipmentChange(kBLineId);
        }
    }
    
    std::map<uint32_t, EquipmentStatus> equipmentStatuses_;
    std::map<uint32_t, ActuatorStatus> actuatorStatuses_;
    std::map<uint32_t, QualityCheckpoint> qualityCheckpoints_;
    /// Quality checkpoint ids whose passRate is currently owned by an
    /// external ingest bridge. tickSimulation() skips the random-walk
    /// update for these so the bridge's last writer stays sticky.
    std::unordered_set<uint32_t> externalPassRateOverrides_;
    WorkUnit currentWorkUnit_;

    /// Top-level production lifecycle. The formal SML transition table
    /// lives in SystemStateMachine.cpp; here we just dispatch commands.
    SystemStateMachine stateMachine_;

    /// Measures the live work-units-per-hour rate from completion
    /// timestamps. Fed steady_clock::now() on every genuine rollover in
    /// tickSimulation; cleared on reset / loadProduct (a new batch).
    ThroughputMeter throughputMeter_;

    /// Monotonic sequence for generating a fresh work-unit id on each
    /// loadProduct() call. Seeded above the demo batch number so the
    /// first loaded batch reads as a plausible continuation.
    uint64_t workUnitSequence_{kWorkUnitSeqBase};
    
    std::vector<EquipmentCallback> equipmentCallbacks_;
    std::vector<ActuatorCallback> actuatorCallbacks_;
    std::vector<QualityCheckpointCallback> qualityCallbacks_;
    std::vector<WorkUnitCallback> workUnitCallbacks_;
    std::vector<StateCallback> stateCallbacks_;
    
    mutable std::mutex mutex_;
    std::mt19937 rng_{kRngSeed};

    // Optional logger -- null when the test harness exercises the model
    // without a real Application; set by Application::initDatabase().
    app::core::Logger* logger_{nullptr};
};

}  // namespace app::model
