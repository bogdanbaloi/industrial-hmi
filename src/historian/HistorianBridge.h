#pragma once

#include "src/historian/HistoryRecord.h"
#include "src/historian/HistoryWriter.h"
#include "src/model/ProductionTypes.h"

#include <chrono>
#include <mutex>
#include <vector>

namespace app::model {
class ProductionModel;
}

namespace app::core {
class Logger;
}

namespace app::historian {

/// Subscribes to a ProductionModel and persists scalar changes via a
/// HistoryWriter.
///
/// Two layers stitched together:
///   - **What** to persist (which model signals -> which series): the
///     `wire()` body. Adding a new series means one new lambda here.
///   - **How** the writes flow to disk: batching window guarded by
///     a mutex, flushed when full or when `flush()` is called by the
///     composition root on shutdown.
///
/// The bridge owns nothing exotic -- the model is non-owning, the
/// writer is non-owning, the in-memory batch is a plain vector. The
/// reason this isn't header-only is to keep the model & writer
/// includes off everything that ever pulls "I might want a bridge".
///
/// SOLID:
///   * S -- one job: translate inbound ProductionModel signals into
///     HistoryRecord rows. No SQL, no schema, no UI.
///   * O -- adding a new series = a new subscribe in `wire()`. The
///     batching path stays untouched.
///   * L -- depends on `HistoryWriter&` (interface). Any concrete
///     plugs in unchanged; tests inject a FakeHistoryWriter that
///     accumulates rows in a vector.
///   * I -- exposes only the two methods the composition root needs:
///     `wire()` once at startup, `flush()` on shutdown. Internal
///     methods stay private.
///   * D -- bridge depends on the writer abstraction, not on
///     SqliteHistoryStore. Same with the model abstraction.
///
/// Threading: model callbacks fire on whichever thread the model
/// dispatches from (model thread for the simulator's tick, ingest
/// threads for inbound MQTT / Modbus / OPC-UA). The bridge serialises
/// onto `mutex_` -- a single critical section per callback to push
/// one row, plus the periodic flush that drains the queue. Flushing
/// happens inline on the calling thread when the batch fills; no
/// dedicated background thread.
class HistorianBridge {
public:
    struct Config {
        /// Maximum rows held in memory before an inline flush. Tuned
        /// against typical simulator cadence (~1 Hz per series, ~6
        /// series == 6 rows/sec): 32 buffers ~5 s of telemetry, which
        /// is short enough that a process crash loses sub-10s of data
        /// and long enough that each transaction amortises the SQLite
        /// commit cost over ~30 rows.
        std::size_t maxBatchSize{32};

        /// Soft cap on a single batch's age before flush. Belt-and-
        /// braces: if a slow model produces fewer than 32 rows before
        /// the operator quits, the destructor's flush() still ships
        /// what we have.
        std::chrono::milliseconds maxBatchAge{std::chrono::seconds{5}};
    };

    HistorianBridge(HistoryWriter& writer,
                    model::ProductionModel& model);
    HistorianBridge(HistoryWriter& writer,
                    model::ProductionModel& model,
                    Config config);

    ~HistorianBridge();

    HistorianBridge(const HistorianBridge&)            = delete;
    HistorianBridge& operator=(const HistorianBridge&) = delete;
    HistorianBridge(HistorianBridge&&)                 = delete;
    HistorianBridge& operator=(HistorianBridge&&)      = delete;

    /// Optional logger injection -- matches the project-wide DI shape.
    void setLogger(app::core::Logger& logger) { logger_ = &logger; }

    /// Subscribe to model signals. Call once after construction (the
    /// composition root usually wires every collaborator before
    /// kicking off the model's first tick).
    void wire();

    /// Drain whatever is currently buffered. Safe to call from any
    /// thread; returns once the writer has accepted the batch. The
    /// destructor calls this so no row is lost on graceful exit.
    void flush();

private:
    /// One row of buffered state. Built by each callback, appended to
    /// `pending_`, flushed in bulk.
    void enqueue(HistoryRecord record);

    /// Caller must hold `mutex_`. Performs the write + clears the
    /// buffer + advances the age epoch. Logs failures but never
    /// throws; a misbehaving writer must not propagate up into a
    /// model callback.
    void flushLocked();

    HistoryWriter&             writer_;
    model::ProductionModel&    model_;
    Config                     config_;
    app::core::Logger*         logger_{nullptr};

    std::mutex                            mutex_;
    std::vector<HistoryRecord>            pending_;
    std::chrono::steady_clock::time_point batchEpoch_{
        std::chrono::steady_clock::now()};
};

}  // namespace app::historian
