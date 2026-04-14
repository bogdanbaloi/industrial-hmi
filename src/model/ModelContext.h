#ifndef MODEL_CONTEXT_H
#define MODEL_CONTEXT_H

#include <boost/asio.hpp>
#include <thread>
#include <memory>
#include <functional>
#include <atomic>
#include "src/core/LoggerBase.h"

namespace app::model {

/// Asynchronous I/O context for database operations
///
/// Provides a single background thread for non-blocking database operations.
/// Uses Boost.Asio io_context to manage async work and prevent UI blocking.
///
/// Pattern: Single I/O thread (not thread pool - database I/O is sequential)
/// Thread-safe: io_context is thread-safe for posting work
/// RAII: std::jthread (C++20) auto-joins on destruction
class ModelContext {
public:
    static ModelContext& instance() {
        static ModelContext inst;
        return inst;
    }

    void setLogger(core::Logger& logger) {
        logger_ = &logger;
    }

    /// Post work to background I/O thread
    /// @return true if posted, false if queue full (backpressure)
    template<typename Handler>
    bool post(Handler&& handler) {
        if (pendingOperations_ >= MAX_PENDING_OPERATIONS) {
            if (logger_) logger_->warn("I/O queue full ({} pending), rejecting operation",
                                        pendingOperations_.load());
            return false;
        }

        ++pendingOperations_;

        boost::asio::post(ioContext_, [this, h = std::forward<Handler>(handler)]() {
            try {
                h();
            } catch (const std::exception& e) {
                if (logger_) logger_->error("Exception in posted I/O task: {}", e.what());
            }
            --pendingOperations_;
        });

        return true;
    }

    /// Stop the I/O context and drain pending work
    void stop() {
        if (stopped_) return;
        stopped_ = true;

        if (logger_) logger_->info("Stopping I/O context ({} pending operations)",
                                    pendingOperations_.load());
        work_.reset();
        ioContext_.stop();
    }

    [[nodiscard]] std::size_t getPendingOperations() const noexcept {
        return pendingOperations_;
    }

    [[nodiscard]] bool isHealthy() const noexcept {
        return !ioContext_.stopped() && pendingOperations_ < MAX_PENDING_OPERATIONS;
    }

    ModelContext(const ModelContext&) = delete;
    ModelContext& operator=(const ModelContext&) = delete;
    ModelContext(ModelContext&&) = delete;
    ModelContext& operator=(ModelContext&&) = delete;

    ~ModelContext() {
        stop();
    }

private:
    ModelContext()
        : work_(boost::asio::make_work_guard(ioContext_))
        , ioThread_([this]() {
            while (!ioContext_.stopped()) {
                try {
                    ioContext_.run();
                } catch (const std::exception& e) {
                    if (logger_) logger_->error("Exception in I/O thread: {}", e.what());
                    if (ioContext_.stopped()) {
                        ioContext_.restart();
                    }
                } catch (...) {
                    if (logger_) logger_->critical("Unknown exception in I/O thread");
                    if (ioContext_.stopped()) {
                        ioContext_.restart();
                    }
                }
            }
        })
    {}

    static constexpr std::size_t MAX_PENDING_OPERATIONS = 1000;

    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    std::jthread ioThread_;
    std::atomic<std::size_t> pendingOperations_{0};
    std::atomic<bool> stopped_{false};
    core::Logger* logger_{nullptr};
};

}  // namespace app::model

#endif  // MODEL_CONTEXT_H
