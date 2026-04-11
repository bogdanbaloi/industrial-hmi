#ifndef MODEL_CONTEXT_H
#define MODEL_CONTEXT_H

#include <boost/asio.hpp>
#include <thread>
#include <memory>
#include <functional>
#include <atomic>
#include "src/core/Logger.h"
#include "src/core/ErrorHandling.h"

namespace app::model {

/**
 * ModelContext - Asynchronous I/O Context for Database Operations
 * 
 * Provides a single background thread for non-blocking database operations.
 * Uses Boost.Asio io_context to manage async work and prevent UI blocking.
 * 
 * Pattern: Single I/O thread (not thread pool - database I/O doesn't need parallelism)
 * Thread-safe: Yes (io_context is thread-safe for posting work)
 * RAII: std::jthread (C++20) - automatic join on destruction
 * Exception-safe: Catches all exceptions in I/O thread, logs and continues
 * 
 * Usage:
 *   ModelContext& ctx = ModelContext::instance();
 *   ctx.post([&db]() {
 *       // This runs on background thread
 *       db.addProductImpl(...);
 *   });
 */
class ModelContext {
public:
    /**
     * Get singleton instance (appropriate for global I/O context)
     */
    static ModelContext& instance() {
        static ModelContext instance;
        return instance;
    }
    
    /**
     * Post work to background I/O thread
     * 
     * @param handler Function to execute on I/O thread
     * @return true if posted successfully, false if queue full
     * 
     * Thread-safe: Can be called from any thread
     * Non-blocking: Returns immediately, handler executes asynchronously
     * Exception-safe: Exceptions in handler are caught and logged
     */
    template<typename Handler>
    bool post(Handler&& handler) {
        // Backpressure: Prevent queue overflow
        if (pendingOperations_ >= MAX_PENDING_OPERATIONS) {
            core::Logger::warn("I/O queue full ({} pending), rejecting operation", 
                              pendingOperations_.load());
            return false;
        }
        
        ++pendingOperations_;
        
        boost::asio::post(ioContext_, [this, h = std::forward<Handler>(handler)]() {
            // Exception guard: Catch all exceptions in I/O thread
            core::ExceptionHandler::safeExecute([&h]() {
                h();
            }, "ModelContext I/O operation");
            
            --pendingOperations_;
        });
        
        return true;
    }
    
    /**
     * Stop the I/O context
     * Note: jthread automatically joins on destruction (RAII)
     */
    void stop() {
        core::Logger::info("Stopping I/O context ({} pending operations)", 
                          pendingOperations_.load());
        ioContext_.stop();
        // jthread automatically joins here
    }
    
    /**
     * Get pending operations count (for monitoring)
     */
    size_t getPendingOperations() const noexcept {
        return pendingOperations_;
    }
    
    /**
     * Check if I/O thread is healthy
     */
    bool isHealthy() const noexcept {
        return !ioContext_.stopped() && pendingOperations_ < MAX_PENDING_OPERATIONS;
    }
    
    // Non-copyable, non-movable (singleton)
    ModelContext(const ModelContext&) = delete;
    ModelContext& operator=(const ModelContext&) = delete;
    ModelContext(ModelContext&&) = delete;
    ModelContext& operator=(ModelContext&&) = delete;
    
    ~ModelContext() {
        stop();
        // jthread automatically joins here - RAII!
    }
    
private:
    ModelContext() 
        : work_(boost::asio::make_work_guard(ioContext_))
        , ioThread_([this]() { 
            core::Logger::info("I/O thread started");
            
            // Exception-safe I/O loop
            while (!ioContext_.stopped()) {
                try {
                    ioContext_.run();
                } catch (const std::exception& e) {
                    core::Logger::error("Exception in I/O thread: {}", e.what());
                    // Don't exit - restart context and continue
                    if (ioContext_.stopped()) {
                        ioContext_.restart();
                    }
                } catch (...) {
                    core::Logger::critical("Unknown exception in I/O thread!");
                    // Emergency recovery
                    if (ioContext_.stopped()) {
                        ioContext_.restart();
                    }
                }
            }
            
            core::Logger::info("I/O thread stopped");
        }) 
    {
        // I/O thread is now running and waiting for work
        // jthread will automatically join when destroyed
    }
    
    static constexpr size_t MAX_PENDING_OPERATIONS = 1000;
    
    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    std::jthread ioThread_;  // C++20 RAII thread - automatic join on destruction
    std::atomic<size_t> pendingOperations_{0};
};

} // namespace app::model

#endif // MODEL_CONTEXT_H
