#include "src/integration/OtaSession.h"

#include <boost/sml.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace sml = boost::sml;

namespace app::integration {

namespace {
/// Message codes, section 4 of the protocol.
constexpr std::uint8_t kInfoReq = 0x01;
constexpr std::uint8_t kBegin   = 0x02;
constexpr std::uint8_t kData    = 0x03;
constexpr std::uint8_t kCommit  = 0x04;
constexpr std::uint8_t kConfirm = 0x05;
constexpr std::uint8_t kInfo    = 0x81;
constexpr std::uint8_t kAck     = 0x82;
constexpr std::uint8_t kNak     = 0x83;

/// The one `NAK` code this class reacts to by name: it means the bytes were
/// damaged on the wire, so the same frame again is the right answer.
constexpr std::uint8_t kNakBadCrc = 0x01;

/// `INFO` payload: version u32, active bank u8, state u8.
constexpr std::size_t  kInfoPayloadBytes = 6;
constexpr std::size_t  kInfoVersionAt    = 0;
constexpr std::size_t  kInfoStateAt      = 5;
constexpr std::uint8_t kStateConfirmed   = 0;
constexpr std::uint8_t kStateTrial       = 1;

/// Image bytes per `DATA` frame, plus the alignment the L4 flash needs.
constexpr std::size_t kDataChunkBytes = 256;
constexpr std::size_t kFlashWordBytes = 8;
/// Padding for the last chunk: the value of erased flash.
constexpr std::byte kErasedByte{0xFF};

constexpr unsigned     kBitsPerByte = 8U;
constexpr std::uint32_t kByteMask   = 0xFFU;

void appendLittleEndian32(std::vector<std::byte>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < sizeof(std::uint32_t) * kBitsPerByte;
         shift += kBitsPerByte) {
        out.push_back(static_cast<std::byte>((value >> shift) & kByteMask));
    }
}

[[nodiscard]] std::uint32_t readLittleEndian32(const std::byte* at) {
    std::uint32_t value = 0;
    for (unsigned index = 0; index < sizeof(std::uint32_t); ++index) {
        value |= std::to_integer<std::uint32_t>(at[index])
                 << (index * kBitsPerByte);
    }
    return value;
}

/// Everything the transition table reads and writes. The table itself holds
/// no data: guards and actions all work on this one object, which is what
/// keeps the table readable as a list of rules.
struct Context {
    std::vector<std::byte> image;
    std::uint32_t          version = 0;
    OtaSession::Timing     timing;

    /// Set by the caller before each dispatch, because the session never
    /// reads a clock of its own.
    OtaSession::TimePoint now{};

    /// What the session wants sent. Collected by the actions, handed back
    /// by the public call that triggered them.
    std::vector<std::byte> outbox;

    /// The frame waiting for an answer, kept for a resend.
    std::vector<std::byte> inFlight;
    std::uint16_t          seq     = 0;
    int                    resends = 0;
    /// How long the frame in flight is given, so a resend arms the same
    /// wait again. `BEGIN` gets a longer one than the rest.
    OtaSession::Duration   waitFor{0};
    OtaSession::TimePoint  deadline{};
    /// True while waiting out the board's reset, when nothing is in flight.
    bool waitingOut = false;

    /// Real image bytes in the `DATA` frame in flight, padding excluded.
    std::size_t   chunkBytes       = 0;
    std::size_t   sentBytes        = 0;
    std::uint32_t boardVersion     = 0;
    std::uint8_t  boardState       = kStateConfirmed;
    bool          alreadyUpToDate  = false;

    OtaFailure   failure = OtaFailure::None;
    std::uint8_t nakCode = 0;

    void send(std::uint8_t type, std::vector<std::byte> payload,
              OtaSession::Duration within) {
        ++seq;
        FlashFrame frame;
        frame.type    = type;
        frame.seq     = seq;
        frame.payload = std::move(payload);
        inFlight      = encodeFlashFrame(frame);
        resends       = 0;
        waitFor       = within;
        deadline      = now + within;
        waitingOut    = false;
        outbox        = inFlight;
    }

    void resend() {
        ++resends;
        deadline = now + waitFor;
        outbox   = inFlight;
    }

    void sendDataChunk() {
        const std::size_t remaining = image.size() - sentBytes;
        chunkBytes = std::min(kDataChunkBytes, remaining);

        std::vector<std::byte> payload;
        appendLittleEndian32(payload, static_cast<std::uint32_t>(sentBytes));
        const auto begin = image.begin() + static_cast<std::ptrdiff_t>(sentBytes);
        payload.insert(payload.end(), begin,
                       begin + static_cast<std::ptrdiff_t>(chunkBytes));
        // The L4 programs 8 bytes at a time, so the last chunk is padded with
        // the value of erased flash. The CRC-32 covers the announced size
        // only, so padding can never change the verdict at COMMIT.
        while ((payload.size() - sizeof(std::uint32_t)) % kFlashWordBytes != 0) {
            payload.push_back(kErasedByte);
        }
        send(kData, std::move(payload), timing.answer);
    }

    void stopWith(OtaFailure why, std::uint8_t code = 0) {
        failure = why;
        nakCode = code;
        inFlight.clear();
        outbox.clear();
    }
};

/* ----- events ------------------------------------------------------- */
struct EvStart {};
struct EvAck {};
struct EvInfo {
    std::uint32_t version = 0;
    std::uint8_t  state   = kStateConfirmed;
};
struct EvNak {
    std::uint8_t code = 0;
};
struct EvTimeout {};
struct EvRebootDone {};

/* ----- guards ------------------------------------------------------- */
constexpr auto kHasImage   = [](Context& c) { return !c.image.empty(); };
constexpr auto kNoImage    = [](Context& c) { return c.image.empty(); };
constexpr auto kIsConfirmedTarget = [](Context& c, const EvInfo& e) {
    return e.version == c.version && e.state == kStateConfirmed;
};
constexpr auto kIsTrialTarget = [](Context& c, const EvInfo& e) {
    return e.version == c.version && e.state == kStateTrial;
};
constexpr auto kNeedsUpdate = [](Context& c, const EvInfo& e) {
    return e.version != c.version;
};
constexpr auto kIsTargetVersion = [](Context& c, const EvInfo& e) {
    return e.version == c.version;
};
constexpr auto kIsOtherVersion = [](Context& c, const EvInfo& e) {
    return e.version != c.version;
};
constexpr auto kMoreToSend = [](Context& c) {
    return c.sentBytes + c.chunkBytes < c.image.size();
};
constexpr auto kAllSent = [](Context& c) {
    return c.sentBytes + c.chunkBytes >= c.image.size();
};
constexpr auto kCanResend = [](Context& c) { return c.resends < c.timing.resends; };
constexpr auto kDamagedAndCanResend = [](Context& c, const EvNak& e) {
    return e.code == kNakBadCrc && c.resends < c.timing.resends;
};
constexpr auto kRefusedOrSpent = [](Context& c, const EvNak& e) {
    return e.code != kNakBadCrc || c.resends >= c.timing.resends;
};

/* ----- actions ------------------------------------------------------ */
constexpr auto kSendInfoReq = [](Context& c) {
    c.send(kInfoReq, {}, c.timing.answer);
};
constexpr auto kSendBegin = [](Context& c, const EvInfo& e) {
    c.boardVersion = e.version;
    c.boardState   = e.state;
    std::vector<std::byte> payload;
    appendLittleEndian32(payload, static_cast<std::uint32_t>(c.image.size()));
    appendLittleEndian32(payload, crc32IsoHdlc(c.image));
    appendLittleEndian32(payload, c.version);
    c.send(kBegin, std::move(payload), c.timing.beginAnswer);
};
constexpr auto kSendFirstChunk = [](Context& c) {
    c.sentBytes  = 0;
    c.chunkBytes = 0;
    c.sendDataChunk();
};
/// An `ACK` means the bytes reached flash, so progress moves only here.
constexpr auto kAckChunkAndSendNext = [](Context& c) {
    c.sentBytes += c.chunkBytes;
    c.sendDataChunk();
};
constexpr auto kAckChunkAndCommit = [](Context& c) {
    c.sentBytes += c.chunkBytes;
    c.chunkBytes = 0;
    c.send(kCommit, {}, c.timing.answer);
};
/// The board switches banks and resets itself, so nothing is sent into it
/// until the reset is over.
constexpr auto kWaitOutReboot = [](Context& c) {
    c.inFlight.clear();
    c.outbox.clear();
    c.waitingOut = true;
    c.deadline   = c.now + c.timing.reboot;
};
constexpr auto kSendConfirm = [](Context& c, const EvInfo& e) {
    c.boardVersion = e.version;
    c.boardState   = e.state;
    c.send(kConfirm, {}, c.timing.answer);
};
constexpr auto kFinish = [](Context& c) {
    c.inFlight.clear();
    c.outbox.clear();
};
constexpr auto kFinishUpToDate = [](Context& c, const EvInfo& e) {
    c.boardVersion    = e.version;
    c.boardState      = e.state;
    c.alreadyUpToDate = true;
    c.inFlight.clear();
    c.outbox.clear();
};
constexpr auto kResendInFlight = [](Context& c) { c.resend(); };
constexpr auto kFailEmptyImage  = [](Context& c) {
    c.stopWith(OtaFailure::ProtocolError);
};
constexpr auto kFailRefused = [](Context& c, const EvNak& e) {
    c.stopWith(OtaFailure::BoardRefused, e.code);
};
constexpr auto kFailSilent = [](Context& c) {
    c.stopWith(OtaFailure::NoAnswer);
};
constexpr auto kFailWrongVersion = [](Context& c, const EvInfo& e) {
    c.boardVersion = e.version;
    c.stopWith(OtaFailure::WrongVersion);
};

/// The rules of one update, as one table. Read it as: in this state, on
/// this event, if this holds, do this and go there.
struct OtaTable {
    auto operator()() const {
        using namespace sml;  // NOLINT(google-build-using-namespace)
        return make_transition_table(
            // ---- the start ------------------------------------------
            *"idle"_s + event<EvStart> [ kHasImage ] / kSendInfoReq
                = "probing"_s,
            "idle"_s + event<EvStart> [ kNoImage ] / kFailEmptyImage
                = "failed"_s,

            // ---- what the board is running --------------------------
            // Already the target version and kept: nothing to do, and
            // saying so beats writing a bank for it.
            "probing"_s + event<EvInfo> [ kIsConfirmedTarget ] / kFinishUpToDate
                = "done"_s,
            // The target version but on trial: the image is already there,
            // it only lacks the confirmation. Sending it again would erase
            // a bank for nothing.
            "probing"_s + event<EvInfo> [ kIsTrialTarget ] / kSendConfirm
                = "confirming"_s,
            "probing"_s + event<EvInfo> [ kNeedsUpdate ] / kSendBegin
                = "beginning"_s,

            // ---- the transfer ---------------------------------------
            "beginning"_s + event<EvAck> / kSendFirstChunk = "sending"_s,
            "sending"_s + event<EvAck> [ kMoreToSend ] / kAckChunkAndSendNext
                = "sending"_s,
            "sending"_s + event<EvAck> [ kAllSent ] / kAckChunkAndCommit
                = "committing"_s,
            "committing"_s + event<EvAck> / kWaitOutReboot = "rebooting"_s,
            "rebooting"_s + event<EvRebootDone> / kSendInfoReq
                = "rechecking"_s,

            // ---- keep it, or refuse to ------------------------------
            "rechecking"_s + event<EvInfo> [ kIsTargetVersion ] / kSendConfirm
                = "confirming"_s,
            // It came back on something else, so confirming would keep the
            // wrong image.
            "rechecking"_s + event<EvInfo> [ kIsOtherVersion ] / kFailWrongVersion
                = "failed"_s,
            "confirming"_s + event<EvAck> / kFinish = "done"_s,

            // ---- a damaged frame, from any waiting state ------------
            // BAD_CRC means the wire damaged the bytes, so the same frame
            // again is the right answer, under the same resend budget.
            "probing"_s + event<EvNak> [ kDamagedAndCanResend ] / kResendInFlight
                = "probing"_s,
            "beginning"_s + event<EvNak> [ kDamagedAndCanResend ] / kResendInFlight
                = "beginning"_s,
            "sending"_s + event<EvNak> [ kDamagedAndCanResend ] / kResendInFlight
                = "sending"_s,
            "committing"_s + event<EvNak> [ kDamagedAndCanResend ] / kResendInFlight
                = "committing"_s,
            "rechecking"_s + event<EvNak> [ kDamagedAndCanResend ] / kResendInFlight
                = "rechecking"_s,
            "confirming"_s + event<EvNak> [ kDamagedAndCanResend ] / kResendInFlight
                = "confirming"_s,

            // ---- a refusal, from any waiting state ------------------
            // Every other NAK is the board deciding. The same bytes would
            // be refused the same way, so the session stops.
            "probing"_s + event<EvNak> [ kRefusedOrSpent ] / kFailRefused
                = "failed"_s,
            "beginning"_s + event<EvNak> [ kRefusedOrSpent ] / kFailRefused
                = "failed"_s,
            "sending"_s + event<EvNak> [ kRefusedOrSpent ] / kFailRefused
                = "failed"_s,
            "committing"_s + event<EvNak> [ kRefusedOrSpent ] / kFailRefused
                = "failed"_s,
            "rechecking"_s + event<EvNak> [ kRefusedOrSpent ] / kFailRefused
                = "failed"_s,
            "confirming"_s + event<EvNak> [ kRefusedOrSpent ] / kFailRefused
                = "failed"_s,

            // ---- silence, from any waiting state --------------------
            "probing"_s + event<EvTimeout> [ kCanResend ] / kResendInFlight
                = "probing"_s,
            "beginning"_s + event<EvTimeout> [ kCanResend ] / kResendInFlight
                = "beginning"_s,
            "sending"_s + event<EvTimeout> [ kCanResend ] / kResendInFlight
                = "sending"_s,
            "committing"_s + event<EvTimeout> [ kCanResend ] / kResendInFlight
                = "committing"_s,
            "rechecking"_s + event<EvTimeout> [ kCanResend ] / kResendInFlight
                = "rechecking"_s,
            "confirming"_s + event<EvTimeout> [ kCanResend ] / kResendInFlight
                = "confirming"_s,

            "probing"_s + event<EvTimeout> / kFailSilent     = "failed"_s,
            "beginning"_s + event<EvTimeout> / kFailSilent   = "failed"_s,
            "sending"_s + event<EvTimeout> / kFailSilent     = "failed"_s,
            "committing"_s + event<EvTimeout> / kFailSilent  = "failed"_s,
            "rechecking"_s + event<EvTimeout> / kFailSilent  = "failed"_s,
            "confirming"_s + event<EvTimeout> / kFailSilent  = "failed"_s

            // Anything else is unhandled on purpose: the wrapper below
            // turns an unhandled event into OtaFailure::ProtocolError, so
            // a frame the protocol does not allow here cannot pass quietly.
        );
    }
};

}  // namespace

// Boost.SML's internal zero-size array is diagnosed where `sml::sm<...>` is
// instantiated, so the suppression wraps the use, not the include. Same
// treatment as app::model::SystemStateMachine.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

struct OtaSession::Impl {
    Context                  ctx;
    sml::sm<OtaTable>        sm;

    explicit Impl(Context context)
        : ctx(std::move(context)), sm{ctx} {}

    /// Dispatch one event and hand back whatever it decided to send. An
    /// event the table does not handle in this state is a protocol error.
    template <typename Event>
    std::vector<std::byte> dispatch(const Event& event, TimePoint now) {
        ctx.now = now;
        ctx.outbox.clear();
        if (!sm.process_event(event)) {
            // No row matched here. The table lists every move the protocol
            // allows, so anything else is a frame that does not belong at
            // this point. `ctx.failure` alone makes `stage()` report Failed,
            // which is why the table is left where it is.
            ctx.stopWith(OtaFailure::ProtocolError);
            return {};
        }
        return ctx.outbox;
    }

    [[nodiscard]] OtaStage stage() const {
        using namespace sml;  // NOLINT(google-build-using-namespace)
        if (ctx.failure != OtaFailure::None) return OtaStage::Failed;
        if (sm.is("done"_s))       return OtaStage::Done;
        if (sm.is("failed"_s))     return OtaStage::Failed;
        if (sm.is("probing"_s))    return OtaStage::Probing;
        if (sm.is("beginning"_s))  return OtaStage::Beginning;
        if (sm.is("sending"_s))    return OtaStage::Sending;
        if (sm.is("committing"_s)) return OtaStage::Committing;
        if (sm.is("rebooting"_s))  return OtaStage::Rebooting;
        if (sm.is("rechecking"_s)) return OtaStage::Rechecking;
        if (sm.is("confirming"_s)) return OtaStage::Confirming;
        return OtaStage::Idle;
    }
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

OtaSession::OtaSession(std::vector<std::byte> image, std::uint32_t version)
    : OtaSession(std::move(image), version, Timing{}) {}

OtaSession::OtaSession(std::vector<std::byte> image, std::uint32_t version,
                       Timing timing) {
    Context ctx;
    ctx.image   = std::move(image);
    ctx.version = version;
    ctx.timing  = timing;
    impl_       = std::make_unique<Impl>(std::move(ctx));
}

OtaSession::~OtaSession() = default;
OtaSession::OtaSession(OtaSession&&) noexcept = default;
OtaSession& OtaSession::operator=(OtaSession&&) noexcept = default;

std::vector<std::byte> OtaSession::start(TimePoint now) {
    if (stage() != OtaStage::Idle) {
        return {};
    }
    impl_->ctx.now = now;
    impl_->ctx.outbox.clear();
    (void)impl_->sm.process_event(EvStart{});
    return impl_->ctx.outbox;
}

std::vector<std::byte> OtaSession::onFrame(const FlashFrame& frame,
                                           TimePoint now) {
    const OtaStage where = stage();
    if (where == OtaStage::Idle || where == OtaStage::Done ||
        where == OtaStage::Failed) {
        return {};
    }
    // An answer to a frame the session has already moved past. The board
    // resends an ACK when it thinks one was lost, so this is normal traffic,
    // not an error. INFO carries the board's own SEQ, so it is exempt.
    if (frame.type != kInfo && frame.seq != impl_->ctx.seq) {
        return {};
    }

    switch (frame.type) {
    case kAck:
        return impl_->dispatch(EvAck{}, now);
    case kNak:
        if (frame.payload.empty()) {
            impl_->ctx.stopWith(OtaFailure::ProtocolError);
            return {};
        }
        return impl_->dispatch(
            EvNak{std::to_integer<std::uint8_t>(frame.payload[0])}, now);
    case kInfo: {
        if (frame.payload.size() != kInfoPayloadBytes) {
            impl_->ctx.stopWith(OtaFailure::ProtocolError);
            return {};
        }
        const EvInfo info{readLittleEndian32(&frame.payload[kInfoVersionAt]),
                          std::to_integer<std::uint8_t>(
                              frame.payload[kInfoStateAt])};
        return impl_->dispatch(info, now);
    }
    default:
        impl_->ctx.stopWith(OtaFailure::ProtocolError);
        return {};
    }
}

std::vector<std::byte> OtaSession::onTick(TimePoint now) {
    const OtaStage where = stage();
    if (where == OtaStage::Idle || where == OtaStage::Done ||
        where == OtaStage::Failed) {
        return {};
    }
    if (now < impl_->ctx.deadline) {
        return {};
    }
    if (impl_->ctx.waitingOut) {
        impl_->ctx.waitingOut = false;
        return impl_->dispatch(EvRebootDone{}, now);
    }
    return impl_->dispatch(EvTimeout{}, now);
}

OtaStage OtaSession::stage() const { return impl_->stage(); }
OtaFailure OtaSession::failure() const { return impl_->ctx.failure; }
std::uint8_t OtaSession::nakCode() const { return impl_->ctx.nakCode; }
std::size_t OtaSession::bytesAcknowledged() const {
    return impl_->ctx.sentBytes;
}
std::size_t OtaSession::imageSize() const { return impl_->ctx.image.size(); }
std::uint32_t OtaSession::boardVersion() const {
    return impl_->ctx.boardVersion;
}
bool OtaSession::wasAlreadyUpToDate() const {
    return impl_->ctx.alreadyUpToDate;
}

std::string OtaSession::failureText() const {
    const Context& ctx = impl_->ctx;
    switch (ctx.failure) {
    case OtaFailure::None:
        return {};
    case OtaFailure::BoardRefused:
        return std::format("the board refused the update, NAK code 0x{:02X}",
                           ctx.nakCode);
    case OtaFailure::NoAnswer:
        return std::format("the board stopped answering after {} resends",
                           ctx.timing.resends);
    case OtaFailure::ProtocolError:
        return ctx.image.empty()
                   ? std::string("the image is empty, nothing to send")
                   : std::string("the board sent a frame the protocol does "
                                 "not allow at this point");
    case OtaFailure::WrongVersion:
        return std::format(
            "after the update the board reports version {}, not {}",
            ctx.boardVersion, ctx.version);
    }
    return {};
}

}  // namespace app::integration
