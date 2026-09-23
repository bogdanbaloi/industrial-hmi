#pragma once

#include "src/integration/FlashFrame.h"
#include "src/integration/OtaSession.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace app::integration {

/// A copy of where one update has got to, safe to read from any thread.
struct OtaProgress {
    OtaStage      stage   = OtaStage::Idle;
    OtaFailure    failure = OtaFailure::None;
    std::string   failureText;
    std::uint8_t  nakCode           = 0;
    std::size_t   bytesAcknowledged = 0;
    std::size_t   imageSize         = 0;
    bool          alreadyUpToDate   = false;
    /// True when the transport refused to send. The session has no event for
    /// a link that dies mid-update, so the agent reports it here rather than
    /// letting the update sit silent until its timeouts run out.
    bool transportFailed = false;
};

/// Runs one `OtaSession` against a real serial link: frames in, bytes out,
/// clock ticked. REQ-INTEGRATION-016, ADR-0035.
///
/// @rationale `OtaSession` decides but touches nothing (ADR-0034), and
/// `SerialBackend` moves bytes but decides nothing (ADR-0032, ADR-0033).
/// Something has to own the pair. Keeping that owner small, and separate
/// from both, is what lets the session stay testable with no port and the
/// backend stay a protocol-agnostic transport with no OTA in it.
///
/// @threading The session is not thread-safe, so it is confined rather than
/// locked: the agent owns its own `io_context` and one thread, and every
/// call into the session happens there. The frame sink handed to
/// `SerialBackend` runs on the backend's thread and does nothing but post
/// the frame across. `progress()` returns a copy taken under a small mutex,
/// so a UI can poll it from anywhere without touching the session.
///
/// @lifecycle `start()` sends the first frame, `stop()` joins the thread and
/// is safe to call twice, and the destructor stops. The sink holds a weak
/// reference, so a frame arriving after the agent is gone is dropped instead
/// of reaching freed state.
class OtaAgent {
public:
    /// How the agent puts bytes on the wire. `SerialBackend::send` fits it.
    /// Returning false means the link is gone.
    using SendFn = std::function<bool(std::span<const std::byte>)>;

    /// Called once, on the agent's thread, when the update finishes or
    /// fails. Optional.
    using DoneFn = std::function<void(const OtaProgress&)>;

    OtaAgent(SendFn send, std::vector<std::byte> image, std::uint32_t version);
    OtaAgent(SendFn send, std::vector<std::byte> image, std::uint32_t version,
             OtaSession::Timing timing, DoneFn onDone);
    ~OtaAgent();

    OtaAgent(const OtaAgent&)            = delete;
    OtaAgent& operator=(const OtaAgent&) = delete;
    OtaAgent(OtaAgent&&)                 = delete;
    OtaAgent& operator=(OtaAgent&&)      = delete;

    /// Start the thread and send the first frame. Idempotent.
    void start();

    /// Stop ticking and join the thread. Idempotent, safe from the
    /// destructor, safe to call while an update is in flight.
    void stop() noexcept;

    /// The sink to hand to `SerialBackend`'s constructor. It may be called
    /// on the backend's thread, and it only posts the frame to the agent's
    /// own thread.
    [[nodiscard]] std::function<void(const FlashFrame&)> frameSink();

    /// A snapshot, safe from any thread.
    [[nodiscard]] OtaProgress progress() const;

private:
    struct Impl;
    /// Shared so the frame sink can hold a weak reference and outlive the
    /// agent without dangling.
    std::shared_ptr<Impl> impl_;
};

}  // namespace app::integration
