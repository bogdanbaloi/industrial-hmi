#pragma once

#include "src/model/ProductionTypes.h"

#include <functional>
#include <memory>
#include <string>

namespace app::model {

/// Top-level production lifecycle as a formal finite state machine.
///
/// The current state is one of `SystemState::{IDLE, RUNNING, ERROR,
/// CALIBRATION}` and advances only via the declarative transition table
/// in `SystemStateMachine.cpp`. The table is expressed with Boost.SML;
/// keeping it (and the heavy template instantiation) inside the .cpp via
/// PIMPL means consumers of this header pay none of the compile-time
/// cost and the model layer never sees `boost/sml.hpp`.
///
/// Why a formal SM instead of raw `currentState_` assignments?
/// - The transition table is a single auditable artefact -- the ASPICE
///   SWE.2/SWE.3 deliverable, not a story scattered across command
///   bodies.
/// - Guards (Phase 2), entry/exit actions (Phase 2/3), and safe-state on
///   fault (Phase 3) all plug into the SM without changing producer
///   callsites.
/// - The observer callback only fires when the SM *actually* transitions,
///   so idempotent commands (e.g. `start` from `RUNNING`) no longer
///   produce phantom view refreshes.
///
/// Thread safety: not internally synchronised; callers (currently the
/// `SimulatedModel` mutex-guarded command methods) provide the
/// serialisation.
class SystemStateMachine {
public:
    using StateCallback = std::function<void(SystemState)>;

    SystemStateMachine();
    ~SystemStateMachine();

    SystemStateMachine(const SystemStateMachine&)            = delete;
    SystemStateMachine& operator=(const SystemStateMachine&) = delete;
    SystemStateMachine(SystemStateMachine&&)                 = delete;
    SystemStateMachine& operator=(SystemStateMachine&&)      = delete;

    // Producer-facing commands -- map onto SML events. Phase 2 tightens
    // the transition table so an invalid combination (e.g. Calibrate from
    // RUNNING) is dropped at the SM level. The Phase 1 permissive
    // transitions are gone; producers must respect the lifecycle.
    void start();
    void stop();
    void reset();
    void calibrate();
    void calibrationDone();

    /// Phase 3 (safe-state). Force the SM into ERROR from any state and
    /// record the reason for downstream consumers (presenter raises an
    /// ISA-18.2 alarm carrying this string). Only `reset()` can leave
    /// ERROR, mirroring the lock-out behaviour an operator expects on a
    /// safety stop -- they must explicitly clear the fault before
    /// production can restart.
    void fault(const std::string& reason);

    [[nodiscard]] SystemState state() const;

    /// Reason supplied to the most recent `fault()` call. Cleared (returns
    /// empty) once the SM leaves ERROR via `reset()`.
    [[nodiscard]] const std::string& lastFaultReason() const;

    /// Append a state-change observer. Fires exactly once per real
    /// transition (no spurious notify for idempotent events). Callbacks
    /// fire on the thread that invoked the dispatch method.
    void onStateChanged(StateCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace app::model
