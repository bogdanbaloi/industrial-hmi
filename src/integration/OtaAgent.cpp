#include "src/integration/OtaAgent.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

// Boost.Asio on Windows pulls in <windows.h>, which #defines ERROR. Nothing
// here needs that macro; undef it to keep the token clean (the same guard
// SerialBackend and the TCP backend use).
#ifdef ERROR
#  undef ERROR
#endif

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace app::integration {

namespace asio = boost::asio;

namespace {
/// How often the agent feeds the session the current time. The session acts
/// only when one of its own deadlines has passed, so this is the resolution
/// of every timeout rather than a period the protocol knows about. Short
/// enough that a test can use millisecond timings, cheap enough that the
/// real 2 s answer budget costs a few hundred wakeups.
constexpr std::chrono::milliseconds kTickInterval{5};
}  // namespace

/// Everything one agent owns. Held by `shared_ptr` so the frame sink and the
/// tick timer can hold a weak reference to it: a frame or a tick arriving
/// after the agent is gone finds an expired `weak_ptr` and is dropped,
/// instead of reaching freed state (ADR-0035).
///
/// A struct with public members, like `SerialIo` and `OtaSession::Impl`: it
/// is one file's private state, and the encapsulation that matters is the
/// pimpl itself.
struct OtaAgent::Impl : std::enable_shared_from_this<OtaAgent::Impl> {
    SendFn     send;
    OtaSession session;
    DoneFn     onDone;

    /// Held by `stop()` across the join, and by `start()` across the
    /// thread's creation. Not just around the latch: a caller is entitled to
    /// tear down whatever `send` captured the moment `stop()` returns, so
    /// every caller has to come out the other side with the agent thread
    /// really gone, not only the first one.
    std::mutex stopMutex;

    /// The agent thread's own id, stored by that thread before it runs the
    /// context. Lets `stop()` recognise a call made from inside a handler,
    /// which can neither join that thread nor wait behind its join.
    /// Declared above `thread` on purpose, so it outlives it.
    std::atomic<std::thread::id> agentThread{};

    /// The agent's own io_context, separate from the transport's. Every call
    /// into `session` happens on the one thread that runs it, which is what
    /// lets the session stay a class with no lock in it.
    asio::io_context   context;
    asio::steady_timer tick;
    std::jthread       thread;

    /// Latched by `start()`, so a second `start()` is a no-op. One agent
    /// runs one update: after `stop()` it stays stopped.
    std::atomic<bool> started{false};
    /// True between `start()` and `stop()`. Guards the join, so two threads
    /// calling `stop()` at once cannot both join.
    std::atomic<bool> running{false};

    /// The published snapshot. Every read and every write of `progress`
    /// happens under this mutex, which is the whole of the agent's
    /// cross-thread state.
    mutable std::mutex progressMutex;
    OtaProgress        progress;

    /// Touched only on the agent thread, folded into `progress` on every
    /// publish. Latched: the link going away once is what an operator needs
    /// told, and nothing at this layer can bring it back.
    bool transportFailed = false;
    /// Touched only on the agent thread. `onDone` fires once, ever.
    bool doneFired = false;

    Impl(SendFn sendFn, std::vector<std::byte> image, std::uint32_t version,
         OtaSession::Timing timing, DoneFn doneFn)
        : send(std::move(sendFn)),
          session(std::move(image), version, timing),
          onDone(std::move(doneFn)),
          tick(context) {
        // Publish once here so `imageSize` is readable before `start()`.
        // Safe on the caller's thread: no other thread exists yet.
        publishProgress();
    }

    ~Impl() { stop(); }

    Impl(const Impl&)            = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&)                 = delete;
    Impl& operator=(Impl&&)      = delete;

    void start() {
        if (started.exchange(true, std::memory_order_acq_rel)) {
            return;  // already started, idempotent
        }
        running.store(true, std::memory_order_release);

        // The first step runs on the agent thread like every other one, so
        // `session` is touched from exactly one thread from the very first
        // call. Queued before run(), so run() has work and does not return
        // at once; from there the tick timer keeps it alive.
        asio::post(context, [weak = weak_from_this()]() {
            if (const std::shared_ptr<Impl> alive = weak.lock()) {
                alive->beginUpdate();
            }
        });

        // jthread, to match SerialBackend. stop() joins it explicitly; the
        // auto-join is the backstop. Created under the lock stop() joins
        // under, so a stop() racing this start() either runs first and
        // leaves no thread to make, or runs second and joins the one made
        // here. Without the lock it could slip through the gap between the
        // two and return while the thread was still being created.
        const std::lock_guard<std::mutex> lock(stopMutex);
        if (!running.load(std::memory_order_acquire)) {
            return;  // stopped before the thread existed
        }
        thread = std::jthread([this]() {
            agentThread.store(std::this_thread::get_id(),
                              std::memory_order_release);
            try {
                context.run();
            }
            // NOLINTNEXTLINE(bugprone-empty-catch)
            catch (...) { /* a worker thread cannot propagate */ }
        });
    }

    void stop() noexcept {
        // A stop() from inside onDone runs on the agent thread itself. It
        // cannot join that thread, and it must not queue behind a join in
        // flight either, because that join is waiting on this very thread.
        // So it ends the run and leaves the join to whoever is outside.
        if (agentThread.load(std::memory_order_acquire) ==
            std::this_thread::get_id()) {
            running.store(false, std::memory_order_release);
            context.stop();
            return;
        }
        try {
            // The lock is held across the join, not just around the latch.
            // A second thread calling stop() blocks here until the first
            // one's join has returned, and only then sees the latch already
            // taken: it leaves with the thread really stopped rather than
            // with a promise that someone else is stopping it.
            const std::lock_guard<std::mutex> lock(stopMutex);
            if (!running.exchange(false, std::memory_order_acq_rel)) {
                return;  // already stopped, or never started: idempotent
            }
            context.stop();
            if (thread.joinable()) {
                thread.join();
            }
        }
        // Shutdown is noexcept by contract and there is nowhere meaningful
        // to surface a stop-time failure.
        // NOLINTNEXTLINE(bugprone-empty-catch)
        catch (...) { /* swallow */ }
    }

    [[nodiscard]] OtaProgress snapshot() const {
        const std::lock_guard<std::mutex> lock(progressMutex);
        return progress;
    }

    /* ----- everything below runs on the agent thread only ------------- */

    void beginUpdate() {
        doSend(session.start(OtaSession::Clock::now()));
        publishProgress();
        if (!fireDoneIfTerminal()) {
            scheduleTick();
        }
    }

    /// Put the session's bytes on the wire. An empty span means the session
    /// had nothing to say, which is the normal case while it waits.
    void doSend(const std::vector<std::byte>& bytes) {
        if (bytes.empty()) {
            return;
        }
        if (!send || !send(std::span<const std::byte>(bytes))) {
            // The session has no event for a link that dies mid-update
            // (ADR-0034 kept it I/O-free), so this is reported beside it
            // rather than pushed into it. The session is left to run its own
            // resend budget out and reach its own terminal state.
            transportFailed = true;
        }
    }

    // scheduleTick() re-arms itself through onTick(). clang-tidy
    // `misc-no-recursion` reads that cycle as recursion, but async_wait
    // returns at once and the io_context calls the handler later on a fresh
    // stack: the same pattern SerialBackend::writeNext() documents.
    // NOLINTNEXTLINE(misc-no-recursion)
    void scheduleTick() {
        tick.expires_after(kTickInterval);
        // The handler holds a weak reference, never a shared one. A shared
        // one would be stored inside `tick`, which is a member of the very
        // object it would keep alive, and nothing could then destroy it.
        tick.async_wait(
            // NOLINTNEXTLINE(misc-no-recursion)
            [weak = weak_from_this()](const boost::system::error_code& code) {
                if (code) {
                    return;  // cancelled by stop(), or the context is gone
                }
                if (const std::shared_ptr<Impl> alive = weak.lock()) {
                    alive->onTick();
                }
            });
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    void onTick() {
        if (!running.load(std::memory_order_acquire)) {
            return;
        }
        doSend(session.onTick(OtaSession::Clock::now()));
        publishProgress();
        if (fireDoneIfTerminal()) {
            return;  // terminal: stop re-arming and let the context drain
        }
        scheduleTick();
    }

    void onFrame(const FlashFrame& frame) {
        if (!running.load(std::memory_order_acquire)) {
            return;
        }
        doSend(session.onFrame(frame, OtaSession::Clock::now()));
        publishProgress();
        // The tick chain notices the terminal stage on its next pass and
        // stops re-arming, so nothing has to be cancelled here.
        (void)fireDoneIfTerminal();
    }

    void publishProgress() {
        OtaProgress next;
        next.stage             = session.stage();
        next.failure           = session.failure();
        next.failureText       = session.failureText();
        next.nakCode           = session.nakCode();
        next.bytesAcknowledged = session.bytesAcknowledged();
        next.imageSize         = session.imageSize();
        next.alreadyUpToDate   = session.wasAlreadyUpToDate();
        next.transportFailed   = transportFailed;

        const std::lock_guard<std::mutex> lock(progressMutex);
        progress = std::move(next);
    }

    /// @return true once the session has stopped, whether it finished or
    ///         failed. `onDone` fires on the first such call only.
    bool fireDoneIfTerminal() {
        const OtaStage where = session.stage();
        if (where != OtaStage::Done && where != OtaStage::Failed) {
            return false;
        }
        if (doneFired) {
            return true;
        }
        doneFired = true;
        if (onDone) {
            onDone(snapshot());
        }
        return true;
    }
};

OtaAgent::OtaAgent(SendFn send, std::vector<std::byte> image,
                   std::uint32_t version)
    : OtaAgent(std::move(send), std::move(image), version,
               OtaSession::Timing{}, DoneFn{}) {}

OtaAgent::OtaAgent(SendFn send, std::vector<std::byte> image,
                   std::uint32_t version, OtaSession::Timing timing,
                   DoneFn onDone)
    : impl_(std::make_shared<Impl>(std::move(send), std::move(image), version,
                                   timing, std::move(onDone))) {}

OtaAgent::~OtaAgent() {
    impl_->stop();
}

void OtaAgent::start() {
    impl_->start();
}

void OtaAgent::stop() noexcept {
    impl_->stop();
}

std::function<void(const FlashFrame&)> OtaAgent::frameSink() {
    return [weak = std::weak_ptr<Impl>(impl_)](const FlashFrame& frame) {
        // Runs on the transport's thread. The lock is what makes that safe:
        // an expired weak_ptr means the agent is gone and the frame is
        // dropped. While the lock is held, `context` is guaranteed alive, so
        // the post below cannot race destruction.
        const std::shared_ptr<Impl> alive = weak.lock();
        if (!alive) {
            return;
        }
        // Weak again inside the posted handler, deliberately: a shared_ptr
        // captured there would live inside the io_context that lives inside
        // the Impl, and that cycle would keep the agent alive for ever.
        asio::post(alive->context, [weak, frame]() {
            if (const std::shared_ptr<Impl> stillAlive = weak.lock()) {
                stillAlive->onFrame(frame);
            }
        });
    };
}

OtaProgress OtaAgent::progress() const {
    return impl_->snapshot();
}

}  // namespace app::integration
