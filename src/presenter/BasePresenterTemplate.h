#pragma once

#include <vector>
#include <algorithm>
#include <mutex>
#include <concepts>
#include <type_traits>

namespace app {

/// Concept: A valid View interface for MVP pattern
/// 
/// @design Compile-time verification that ViewInterface has:
///         - Default constructor
///         - Virtual destructor (polymorphism)
///         - Is abstract (has pure virtual methods)
///
/// @example
///    ```cpp
///    class DashboardView {
///        virtual ~DashboardView() = default;
///        virtual void onSomeEvent() = 0;  // Pure virtual
///    };
///    static_assert(ViewInterfaceConcept<DashboardView>);  // ✅ Compiles
///    ```
template<typename T>
concept ViewInterfaceConcept = 
    std::is_abstract_v<T> &&                    // Must have pure virtual methods
    std::has_virtual_destructor_v<T> &&         // Must have virtual destructor
    std::is_default_constructible_v<T> &&       // Must have default constructor
    !std::is_copy_constructible_v<T> &&         // Must NOT be copyable
    !std::is_move_constructible_v<T>;           // Must NOT be movable

/// Base class template for all Presenters in the MVP architecture
///
/// @tparam ViewInterface Pure interface type satisfying ViewInterfaceConcept
///
/// @design C++20 Modern Features:
///         - Concepts for compile-time interface validation
///         - [[nodiscard]] for return values that shouldn't be ignored
///         - constexpr for compile-time evaluation
///         - noexcept specifications for performance
///
/// @design SOLID Principles:
///         - Single Responsibility: Only manages observers + notifications
///         - Open/Closed: Extensible via derivation
///         - Liskov Substitution: Any derived presenter is a BasePresenter
///         - Interface Segregation: Works with any focused interface
///         - Dependency Inversion: Depends on ViewInterface abstraction
///
/// @pattern MVP Architecture:
///          - Model: Business logic, state machines, data sources
///          - View: UI components implementing ViewInterface
///          - Presenter: Mediates between Model and View (this class)
///
/// @threading All methods are thread-safe (mutex-protected)
///
/// @lifetime Observers must outlive their registration
///
/// @example
///    ```cpp
///    class DashboardPresenter : public BasePresenter<DashboardView> {
///    public:
///        void initialize() override {
///            model.onStateChanged([this](State s) {
///                auto vm = createViewModel(s);
///                notifyAll(&DashboardView::onWorkUnitChanged, vm);
///            });
///        }
///    };
///    ```
template<ViewInterfaceConcept ViewInterface>
class BasePresenter {
public:
    /// Default constructor
    /// @design constexpr allows compile-time construction if possible
    constexpr BasePresenter() noexcept = default;
    
    /// Virtual destructor for polymorphism
    /// @design RAII: Ensures derived class destructors are called
    virtual ~BasePresenter() = default;
    
    // Rule of Five: Non-copyable, non-movable
    // C++20: Use = delete for explicit intent
    BasePresenter(const BasePresenter&) = delete;
    BasePresenter& operator=(const BasePresenter&) = delete;
    BasePresenter(BasePresenter&&) = delete;
    BasePresenter& operator=(BasePresenter&&) = delete;
    
    /// Initialize the presenter
    /// @design Pure virtual - derived classes must implement
    /// @note Called after construction, before first use
    virtual void initialize() = 0;
    
    /// Register an observer to receive notifications
    /// @param observer Non-null pointer to ViewInterface implementation
    /// @threading Thread-safe via mutex (RAII scoped_lock)
    /// @lifetime Observer must remain valid until removeObserver() called
    /// @note Duplicate registration is silently ignored
    /// 
    /// @design C++17: std::scoped_lock (better than lock_guard)
    ///         - Can lock multiple mutexes (avoids deadlock)
    ///         - RAII: automatic unlock on scope exit
    void addObserver(ViewInterface* const observer) noexcept
        requires std::is_pointer_v<ViewInterface*>
    {
        if (!observer) [[unlikely]] return;
        
        // C++17: std::scoped_lock > std::lock_guard
        // Benefits: Can lock multiple mutexes, prevents deadlock
        std::scoped_lock const lock(observersMutex_);
        
        // Prevent duplicate registration
        if (auto it = std::ranges::find(observers_, observer); 
            it == observers_.end()) 
        {
            observers_.push_back(observer);
        }
    }
    
    /// Unregister an observer
    /// @param observer Pointer to previously registered observer
    /// @threading Thread-safe via mutex
    /// @note Safe to call even if observer was never registered
    void removeObserver(ViewInterface* const observer) noexcept {
        if (!observer) [[unlikely]] return;
        
        std::scoped_lock const lock(observersMutex_);
        
        // C++20 ranges: std::ranges::remove + erase idiom
        auto [first, last] = std::ranges::remove(observers_, observer);
        observers_.erase(first, last);
    }
    
    /// Get number of registered observers
    /// @return Observer count
    /// @threading Thread-safe via mutex
    /// @design C++20: [[nodiscard]] - result should not be ignored
    [[nodiscard]] std::size_t observerCount() const noexcept {
        std::scoped_lock const lock(observersMutex_);
        return observers_.size();
    }

protected:
    /// List of registered observers
    /// @threading Protected by observersMutex_
    /// @design Raw pointers: Views own themselves, Presenter just observes
    std::vector<ViewInterface*> observers_;
    
    /// Mutex protecting observer list
    /// @design RAII locking with std::scoped_lock (C++17+)
    /// @pattern Resource Acquisition Is Initialization
    /// @note std::scoped_lock preferred over std::lock_guard:
    ///       - Can lock multiple mutexes atomically
    ///       - Prevents deadlock in complex scenarios
    ///       - Same zero-cost abstraction as lock_guard
    mutable std::mutex observersMutex_;
    
    /// Notify all observers by calling a ViewInterface method
    /// @tparam MethodPtr Pointer-to-member-function of ViewInterface
    /// @tparam Args Argument types (deduced)
    /// @param method Member function to call
    /// @param args Arguments to forward (perfect forwarding)
    /// @threading Thread-safe via observersMutex_
    /// @exception noexcept Observers must not throw
    ///
    /// @design C++20: Requires clause validates method signature
    ///
    /// @example
    ///    ```cpp
    ///    WorkUnitViewModel const vm = createViewModel();
    ///    notifyAll(&DashboardView::onWorkUnitChanged, vm);
    ///    ```
    template<typename MethodPtr, typename... Args>
        requires std::is_member_function_pointer_v<MethodPtr>
    void notifyAll(MethodPtr const method, Args&&... args) const noexcept {
        std::scoped_lock const lock(observersMutex_);
        
        for (auto* const observer : observers_) {
            if (observer) [[likely]] {
                // Call: observer->method(args...)
                (observer->*method)(std::forward<Args>(args)...);
            }
        }
    }
    
    /// Notify all observers using a callable
    /// @tparam Callable Lambda or function type: void(ViewInterface*)
    /// @param func Callable invoked for each observer
    /// @threading Thread-safe via observersMutex_
    /// @exception noexcept Callable must not throw
    ///
    /// @design C++20: Requires clause validates callable signature
    ///
    /// @example
    ///    ```cpp
    ///    notifyAll([&vm](DashboardView* const view) {
    ///        view->onWorkUnitChanged(vm);
    ///    });
    ///    ```
    template<typename Callable>
        requires std::invocable<Callable, ViewInterface*>
    void notifyAll(Callable&& func) const noexcept {
        std::scoped_lock const lock(observersMutex_);
        
        // C++20: ranges::for_each could be used here
        for (auto* const observer : observers_) {
            if (observer) [[likely]] {
                std::invoke(std::forward<Callable>(func), observer);
            }
        }
    }
};

// Compile-time verification examples (in unit tests):
// static_assert(ViewInterfaceConcept<DashboardView>);
// static_assert(ViewInterfaceConcept<ProductsView>);

}  // namespace app
