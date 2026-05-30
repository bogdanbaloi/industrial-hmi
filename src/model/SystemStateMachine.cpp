#include "src/model/SystemStateMachine.h"

#include <boost/sml.hpp>

#include <utility>
#include <vector>

namespace app::model {

namespace {
namespace sml = boost::sml;

// Events -- one struct per producer-facing command.
struct StartEvt {};
struct StopEvt {};
struct ResetEvt {};
struct CalibrateEvt {};
struct CalibrationDoneEvt {};
struct FaultEvt {};   // Phase 3 -- safe-state entry; reason is stored on Impl.

/// The formal Phase 2 + 3 transition table.
///
/// States: idle (initial *), running, calibration, error.
/// Phase 2 tightens the table so invalid combinations are dropped at the
/// SML level (no matching transition -> SML silently ignores the event).
/// Phase 3 adds the Fault entry (*-> error) and locks error so only
/// Reset can clear it -- an operator must explicitly recover.
struct SystemStateTable {
    auto operator()() const {
        using namespace sml;
        return make_transition_table(
            // ---- from idle ----------------------------------------
            *"idle"_s + event<StartEvt>     = "running"_s,
             "idle"_s + event<CalibrateEvt> = "calibration"_s,
             "idle"_s + event<ResetEvt>     = "idle"_s,   // idempotent

            // ---- from running -------------------------------------
             "running"_s + event<StopEvt>   = "idle"_s,
             "running"_s + event<ResetEvt>  = "idle"_s,
             "running"_s + event<StartEvt>  = "running"_s,  // idempotent

            // ---- from calibration ---------------------------------
             "calibration"_s + event<CalibrationDoneEvt> = "idle"_s,
             "calibration"_s + event<StopEvt>            = "idle"_s,
             "calibration"_s + event<ResetEvt>           = "idle"_s,

            // ---- safe-state (Phase 3) -----------------------------
            // Fault from ANY state -> ERROR. Once in ERROR only Reset
            // can leave it; every other command is dropped, mirroring
            // the lock-out behaviour an operator expects on a fault.
             "idle"_s        + event<FaultEvt> = "error"_s,
             "running"_s     + event<FaultEvt> = "error"_s,
             "calibration"_s + event<FaultEvt> = "error"_s,
             "error"_s       + event<ResetEvt> = "idle"_s

            // NOTE: previously permissive transitions deliberately
            // OMITTED in Phase 2: RUNNING+Calibrate (stop first),
            // CALIBRATION+Start (reset first), ERROR+Start, ERROR+Stop.
        );
    }
};

}  // namespace

// Boost.SML's internal zero-size array (__BOOST_SML_ZERO_SIZE_ARRAY) is
// diagnosed at the point where `sml::sm<...>` is instantiated -- so the
// suppression has to wrap the use, not the include. SYSTEM-include alone
// is insufficient on GCC 13. Scoped tightly to this struct so the rest
// of the file stays under the project's -Wpedantic -Werror gate.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

struct SystemStateMachine::Impl {
    sml::sm<SystemStateTable>  sm;
    std::vector<StateCallback> observers;
    SystemState                lastNotified{SystemState::IDLE};
    std::string                lastFaultReason;  ///< empty unless in ERROR

    [[nodiscard]] SystemState currentState() const {
        using namespace sml;
        if (sm.is("running"_s))     return SystemState::RUNNING;
        if (sm.is("calibration"_s)) return SystemState::CALIBRATION;
        if (sm.is("error"_s))       return SystemState::ERROR;
        return SystemState::IDLE;
    }

    void afterDispatch() {
        const SystemState now = currentState();
        if (now == lastNotified) {
            return;
        }
        lastNotified = now;
        for (auto& cb : observers) {
            cb(now);
        }
    }
};

SystemStateMachine::SystemStateMachine()
    : impl_(std::make_unique<Impl>()) {}

SystemStateMachine::~SystemStateMachine() = default;

void SystemStateMachine::start() {
    impl_->sm.process_event(StartEvt{});
    impl_->afterDispatch();
}

void SystemStateMachine::stop() {
    impl_->sm.process_event(StopEvt{});
    impl_->afterDispatch();
}

void SystemStateMachine::reset() {
    // Reset clears the lock-out: the recorded fault reason goes with it.
    impl_->lastFaultReason.clear();
    impl_->sm.process_event(ResetEvt{});
    impl_->afterDispatch();
}

void SystemStateMachine::fault(const std::string& reason) {
    impl_->lastFaultReason = reason;
    impl_->sm.process_event(FaultEvt{});
    impl_->afterDispatch();
}

void SystemStateMachine::calibrate() {
    impl_->sm.process_event(CalibrateEvt{});
    impl_->afterDispatch();
}

void SystemStateMachine::calibrationDone() {
    impl_->sm.process_event(CalibrationDoneEvt{});
    impl_->afterDispatch();
}

SystemState SystemStateMachine::state() const {
    return impl_->currentState();
}

const std::string& SystemStateMachine::lastFaultReason() const {
    return impl_->lastFaultReason;
}

void SystemStateMachine::onStateChanged(StateCallback callback) {
    impl_->observers.push_back(std::move(callback));
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

}  // namespace app::model
