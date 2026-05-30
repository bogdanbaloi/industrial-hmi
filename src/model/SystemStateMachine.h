#pragma once

#include "src/model/ProductionTypes.h"

#include <functional>
#include <memory>

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

    // Producer-facing commands -- map onto SML events. The internal
    // transition table decides whether (and to which state) the SM
    // advances; a command issued from a state with no matching
    // transition is silently dropped (Phase 1 -- guards arrive in
    // Phase 2 and will additionally log + audit invalid attempts).
    void start();
    void stop();
    void reset();
    void calibrate();
    void calibrationDone();

    [[nodiscard]] SystemState state() const;

    /// Append a state-change observer. Fires exactly once per real
    /// transition (no spurious notify for idempotent events). Callbacks
    /// fire on the thread that invoked the dispatch method.
    void onStateChanged(StateCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace app::model
