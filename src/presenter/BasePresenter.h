#pragma once

#include "ViewObserver.h"
#include <vector>
#include <algorithm>
#include <mutex>

namespace app {

/// Base class for all Presenters in the MVP architecture
///
/// @design This class implements the core Observer pattern mechanics:
///         - Observer registration/unregistration
///         - Thread-safe observer management
///         - Helper methods for notifying all observers
///
/// @pattern In MVP architecture:
///          - Model: Business logic, state machines, data sources
///          - View: UI components (GTK widgets)
///          - Presenter (this): Mediates between Model and View
///
/// @thread_safety Observer list is protected by mutex.
///                Notification methods are thread-safe.
///
/// @example Derived presenter usage:
///    ```cpp
///    class DashboardPresenter : public BasePresenter {
///    public:
///        void initialize() override {
///            // Subscribe to Model signals
///            mainStateMachine.connectStateChanged([this](State s) {
///                handleStateChanged(s);
///            });
///        }
///        
///    private:
///        void handleStateChanged(State newState) {
///            // Transform Model data to ViewModel
///            auto vm = computeControlPanelState(newState);
///            
///            // Notify all observers
///            notifyControlPanelChanged(vm);
///        }
///        
///        void notifyControlPanelChanged(const ControlPanelViewModel& vm) {
///            std::lock_guard<std::mutex> lock(observersMutex_);
///            for (auto* observer : observers_) {
///                if (observer) {
///                    observer->onControlPanelChanged(vm);
///                }
///            }
///        }
///    };
///    ```
class BasePresenter {
public:
    BasePresenter() = default;
    virtual ~BasePresenter() = default;

    // Delete copy/move - Presenters should be singletons or carefully managed
    BasePresenter(const BasePresenter&) = delete;
    BasePresenter& operator=(const BasePresenter&) = delete;
    BasePresenter(BasePresenter&&) = delete;
    BasePresenter& operator=(BasePresenter&&) = delete;

    /// Initialize the presenter
    /// @note Derived classes override this to subscribe to Model signals
    virtual void initialize() = 0;

    /// Register an observer to receive notifications
    /// @param observer Pointer to ViewObserver implementation (e.g., GTK page widget)
    /// @thread_safety Thread-safe via mutex
    /// @note Observer must remain valid for the lifetime of registration
    ///       Call removeObserver() before destroying the observer
    virtual void addObserver(ViewObserver* observer) {
        if (!observer) return;
        
        std::lock_guard<std::mutex> lock(observersMutex_);
        
        // Avoid duplicate registration
        auto it = std::find(observers_.begin(), observers_.end(), observer);
        if (it == observers_.end()) {
            observers_.push_back(observer);
        }
    }

    /// Unregister an observer
    /// @param observer Pointer to previously registered observer
    /// @thread_safety Thread-safe via mutex
    /// @note Safe to call even if observer was never registered
    virtual void removeObserver(ViewObserver* observer) {
        if (!observer) return;
        
        std::lock_guard<std::mutex> lock(observersMutex_);
        
        auto it = std::find(observers_.begin(), observers_.end(), observer);
        if (it != observers_.end()) {
            observers_.erase(it);
        }
    }

protected:
    /// List of registered observers
    /// @note Protected by observersMutex_
    std::vector<ViewObserver*> observers_;

    /// Mutex protecting observer list
    /// @design Use std::lock_guard for RAII-style locking:
    ///         ```cpp
    ///         {
    ///             std::lock_guard<std::mutex> lock(observersMutex_);
    ///             // Access observers_ here
    ///         }  // Mutex automatically released
    ///         ```
    std::mutex observersMutex_;

    /// Helper: Notify all observers with a method call
    /// @tparam MethodPtr Pointer-to-member-function type
    /// @tparam Args Argument types for the method
    /// @param method Pointer to ViewObserver method (e.g., &ViewObserver::onOrderInfoChanged)
    /// @param args Arguments to forward to the method
    ///
    /// @example
    ///    ```cpp
    ///    void notifyOrderInfoChanged(const OrderInfoViewModel& vm) {
    ///        notifyAll(&ViewObserver::onOrderInfoChanged, vm);
    ///    }
    ///    ```
    ///
    /// @thread_safety Thread-safe via observersMutex_
    template<typename MethodPtr, typename... Args>
    void notifyAll(MethodPtr method, Args&&... args) {
        std::lock_guard<std::mutex> lock(observersMutex_);
        
        for (auto* observer : observers_) {
            if (observer) {
                // Call observer->method(args...)
                (observer->*method)(std::forward<Args>(args)...);
            }
        }
    }
};

}  // namespace app
