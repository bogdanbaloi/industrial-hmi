#ifndef MODEL_CONTEXT_H
#define MODEL_CONTEXT_H

#include <boost/asio.hpp>
#include <thread>
#include <memory>
#include <functional>

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
     * 
     * Thread-safe: Can be called from any thread
     * Non-blocking: Returns immediately, handler executes asynchronously
     */
    template<typename Handler>
    void post(Handler&& handler) {
        boost::asio::post(ioContext_, std::forward<Handler>(handler));
    }
    
    /**
     * Stop the I/O context
     * Note: jthread automatically joins on destruction (RAII)
     */
    void stop() {
        ioContext_.stop();
        // No need to manually join - jthread does it automatically!
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
            ioContext_.run(); 
        }) 
    {
        // I/O thread is now running and waiting for work
        // jthread will automatically join when destroyed
    }
    
    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    std::jthread ioThread_;  // C++20 RAII thread - automatic join on destruction
};

} // namespace app::model

#endif // MODEL_CONTEXT_H
