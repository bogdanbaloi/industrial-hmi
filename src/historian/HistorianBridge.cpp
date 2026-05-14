#include "src/historian/HistorianBridge.h"

#include "src/core/LoggerBase.h"
#include "src/model/ProductionModel.h"

#include <chrono>
#include <utility>

namespace app::historian {

namespace {

/// Wall-clock ms since the Unix epoch. Lives at this layer (rather
/// than in the model or in HistoryRecord) so production code and
/// tests can agree on the source of truth -- tests that want
/// deterministic timestamps pass them in via the record directly and
/// skip the bridge.
std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace

HistorianBridge::HistorianBridge(HistoryWriter& writer,
                                 model::ProductionModel& model)
    : HistorianBridge(writer, model, Config{}) {}

HistorianBridge::HistorianBridge(HistoryWriter& writer,
                                 model::ProductionModel& model,
                                 Config config)
    : writer_(writer),
      model_(model),
      config_(config) {
    pending_.reserve(config_.maxBatchSize);
}

HistorianBridge::~HistorianBridge() {
    // Inline destructor flush so the operator can never lose the
    // last batch by quitting normally. A crash still loses up to
    // `maxBatchAge` seconds; the existing trade-off documented in
    // SqliteHistoryStore (synchronous=NORMAL).
    flush();
}

void HistorianBridge::wire() {
    // Quality pass rate per checkpoint. SimulatedModel emits this on
    // every tick and on every external setQualityPassRate call (MQTT,
    // Modbus, OPC-UA inbound).
    model_.onQualityCheckpointChanged(
        [this](const model::QualityCheckpoint& qc) {
            enqueue(HistoryRecord{
                .timestampMs = nowMs(),
                .field       = FieldKind::QualityPassRate,
                .entityId    = qc.checkpointId,
                .value       = qc.passRate});
        });

    // Equipment supply level (consumables / feed rate). One series per
    // equipment slot.
    model_.onEquipmentStatusChanged(
        [this](const model::EquipmentStatus& es) {
            enqueue(HistoryRecord{
                .timestampMs = nowMs(),
                .field       = FieldKind::EquipmentSupplyLevel,
                .entityId    = es.equipmentId,
                .value       = static_cast<float>(es.supplyLevel)});
        });

    // Top-level system state. Single global series (entityId = 0).
    // Stored as float for schema uniformity; the History page renders
    // this with step interpolation rather than a continuous line.
    model_.onSystemStateChanged(
        [this](model::SystemState s) {
            enqueue(HistoryRecord{
                .timestampMs = nowMs(),
                .field       = FieldKind::SystemState,
                .entityId    = 0,
                .value       = static_cast<float>(s)});
        });

    if (logger_ != nullptr) {
        logger_->info("Historian: bridge wired (batch={}, age<={}ms)",
                      config_.maxBatchSize, config_.maxBatchAge.count());
    }
}

void HistorianBridge::flush() {
    const std::scoped_lock lock(mutex_);
    flushLocked();
}

void HistorianBridge::enqueue(HistoryRecord record) {
    const std::scoped_lock lock(mutex_);
    pending_.push_back(record);

    const auto now = std::chrono::steady_clock::now();
    const bool full = pending_.size() >= config_.maxBatchSize;
    const bool stale = (now - batchEpoch_) >= config_.maxBatchAge;
    if (full || stale) {
        flushLocked();
    }
}

void HistorianBridge::flushLocked() {
    if (pending_.empty()) return;

    const std::size_t accepted = writer_.write(pending_);
    if (accepted != pending_.size() && logger_ != nullptr) {
        // Partial write: log so an operator can correlate with disk-
        // full / permission warnings. The bridge does *not* retry --
        // re-queueing would risk an unbounded backlog if the writer is
        // permanently broken, and stale telemetry is rarely worth
        // recovering after the fact.
        logger_->warn("Historian: partial flush ({} of {} accepted)",
                      accepted, pending_.size());
    }
    pending_.clear();
    batchEpoch_ = std::chrono::steady_clock::now();
}

}  // namespace app::historian
