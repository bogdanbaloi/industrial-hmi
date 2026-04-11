# Architecture Documentation

## Overview

This document describes the technical architecture of the Industrial HMI Control System, including design decisions, patterns, and rationale.

## Table of Contents

1. [Architecture Patterns](#architecture-patterns)
2. [Threading Model](#threading-model)
3. [Data Flow](#data-flow)
4. [State Management](#state-management)
5. [Performance Considerations](#performance-considerations)

---

## Architecture Patterns

### MVP (Model-View-Presenter)

The system follows a strict MVP architecture to ensure:
- **Testability**: Business logic can be unit tested without UI
- **Maintainability**: Changes to UI don't affect business logic and vice versa
- **Flexibility**: UI framework can be swapped without changing core logic

**Layer Responsibilities:**

```
┌─────────────────────────────────────────────────────┐
│ MODEL (Data & Business Logic)                       │
│  - State machines                                   │
│  - OPC-UA client (PLC communication)                │
│  - Database operations (SQLite)                     │
│  - File system monitoring                           │
│  - Hardware abstraction                             │
└─────────────────────────────────────────────────────┘
              ↓ signals                ↑ commands
┌─────────────────────────────────────────────────────┐
│ PRESENTER (Orchestration & Transformation)          │
│  - Subscribe to Model signals                       │
│  - Transform raw data → ViewModels                  │
│  - Handle user actions → Model commands             │
│  - Implement business rules                         │
│  - NO UI dependencies                               │
└─────────────────────────────────────────────────────┘
              ↓ notify                ↑ user actions
┌─────────────────────────────────────────────────────┐
│ VIEW (UI Rendering)                                 │
│  - GTK4/Gtkmm widgets                               │
│  - Observe Presenter via ViewObserver interface     │
│  - Render ViewModels                                │
│  - Capture user input                               │
│  - NO business logic                                │
└─────────────────────────────────────────────────────┘
```

### Observer Pattern

**Why Observer instead of direct coupling?**

```cpp
// ❌ BAD: Direct coupling
class Presenter {
    DashboardPage* view;  // Tight coupling!
    
    void onDataChanged() {
        view->updateDisplay();  // Knows about specific View type
    }
};

// ✅ GOOD: Observer pattern
class Presenter {
    std::vector<ViewObserver*> observers;  // Loose coupling
    
    void onDataChanged() {
        for (auto* obs : observers) {
            obs->onDataChanged(viewModel);  // Generic interface
        }
    }
};
```

**Benefits:**
- Presenter doesn't know about specific View implementations
- Multiple Views can observe the same Presenter (main UI + debug console)
- Easy to add new Views without modifying Presenter
- Testability: Mock observers in unit tests

### ViewModel (DTO) Pattern

ViewModels are pure data structures (DTOs) that:
- Contain only display-ready data (no business logic)
- Are immutable after construction (passed by const-ref)
- Implement equality operators for caching
- Have no dependencies on UI framework

**Example:**

```cpp
struct EquipmentCardViewModel {
    uint32_t equipmentId;
    EquipmentCardStatus status;
    std::string consumables;
    std::string messageStatus;
    bool enabled;
    
    // Equality for caching
    bool operator!=(const EquipmentCardViewModel& other) const {
        return status != other.status || enabled != other.enabled;
    }
};
```

---

## Threading Model

### Why Multi-threaded?

**Problem:** 
- OPC-UA network I/O blocks (can take 100ms+ for PLC response)
- Database queries can take 50-200ms
- State machine logic can take 10-50ms
- UI must remain responsive (60fps = 16ms per frame)

**Solution:** 4-thread architecture

```
┌────────────────┐
│ Hardware Thread│  ← OPC-UA client, sensor polling
│  (Background)  │  ← Network I/O, blocking calls OK
└────────────────┘
        ↓ signals
┌────────────────┐
│  App Thread    │  ← State machines, workflow logic
│  (Background)  │  ← Database writes (SQLite single-writer)
└────────────────┘
        ↓ Boost::Signals2
┌────────────────┐
│Presenter Thread│  ← Data transformation
│  (Background)  │  ← ViewModel creation
└────────────────┘
        ↓ Glib::signal_idle()
┌────────────────┐
│   UI Thread    │  ← GTK main loop
│  (Main Loop)   │  ← Widget rendering ONLY
└────────────────┘
```

### Thread Safety Mechanisms

**1. Glib::signal_idle() - Marshal to UI Thread**

```cpp
// Called from Presenter thread
void Presenter::handleDataUpdate() {
    auto vm = createViewModel();
    
    // Marshal to UI thread
    Glib::signal_idle().connect_once([this, vm]() {
        // This runs on GTK main thread
        notifyObservers(vm);
    });
}
```

**2. Boost::Signals2 - Thread-safe Signals**

```cpp
// Model emits signals from hardware thread
boost::signals2::signal<void(std::string)> workUnitSignal;

// Presenter subscribes from any thread (thread-safe)
workUnitSignal.connect([this](std::string id) {
    handleWorkUnit(id);
});
```

**3. std::mutex - Protect Shared State**

```cpp
class Presenter {
    std::mutex controlPanelMutex_;
    ControlPanelViewModel lastState_;
    
    void updateState() {
        std::lock_guard<std::mutex> lock(controlPanelMutex_);
        // Atomically update state
        lastState_ = computeNewState();
    }
};
```

**4. std::atomic - Lock-free Flags**

```cpp
std::atomic<bool> waitingForActuatorHome_{false};
// Can be read/written from multiple threads without locks
```

---

## Data Flow

### Typical Flow: PLC Update → UI Update

```
1. PLC sends "actuator status" via OPC-UA
                    ↓
2. OpcuaController receives on hardware thread
   → Parses raw data
   → Emits actuatortatusChanged(id, status) signal
                    ↓
3. Main state machine receives signal on app thread
   → Updates internal state
   → Emits stateChanged() signal
                    ↓
4. Presenter receives signal on presenter thread
   → Queries additional data from database
   → Transforms raw data → ViewModel
   → Checks cache: if (vm != lastVm)
   → Calls Glib::signal_idle()
                    ↓
5. GTK main thread callback executes
   → Calls observer->onActuatorCardChanged(vm)
   → View updates widget: operation->set_text(vm.status)
```

**Why so many layers?**
- Each layer has single responsibility
- Enables testing at each level
- Prevents UI blocking on slow operations
- Allows caching/optimization at each stage

---

## State Management

### Control Panel State Machine

The control panel buttons use a state machine to determine availability:

```cpp
ControlPanelViewModel computeState() {
    ControlPanelViewModel vm;
    auto state = Main::getState();
    bool actuatorHome = checkActuator();
    bool hasErrors = checkErrors();
    
    switch (state) {
        case IDLE:
            vm.startEnabled = actuatorHome && !hasErrors;
            vm.stopEnabled = false;
            vm.resetEnabled = true;
            vm.calibrationEnabled = actuatorHome;
            break;
        case RUNNING:
            vm.startEnabled = false;
            vm.stopEnabled = true;
            vm.resetEnabled = false;
            vm.calibrationEnabled = false;
            break;
        case ERROR:
            vm.startEnabled = false;
            vm.stopEnabled = false;
            vm.resetEnabled = true;  // Must reset to recover
            vm.calibrationEnabled = false;
            break;
    }
    return vm;
}
```

**State Transitions:**

```
    IDLE ←→ RUNNING
     ↓         ↓
   ERROR ←──────┘
     ↓
   RESET → IDLE
```

---

## Performance Considerations

### 1. ViewModel Caching

**Problem:** OPC-UA sends updates at 10Hz even when values don't change.

**Solution:** Cache previous ViewModel and compare before notifying.

```cpp
void handleUpdate(const Data& data) {
    auto vm = createViewModel(data);
    
    if (vm != lastVm_) {  // Only notify on actual changes
        lastVm_ = vm;
        notifyObservers(vm);
    }
}
```

**Result:** Reduced UI updates from 10/sec to ~2/sec (5x improvement)

### 2. Database Optimization

**Before:**
```cpp
// 3 separate queries (200ms total)
auto product = db.query("SELECT * FROM product WHERE id=?", id);
auto operation = db.query("SELECT * FROM operation_specs WHERE productId=?", id);
auto history = db.query("SELECT * FROM workUnit_history WHERE productId=?", id);
```

**After:**
```cpp
// Single JOIN query (15ms)
auto result = db.query(
    "SELECT a.*, l.*, h.* "
    "FROM product a "
    "LEFT JOIN operation_specs l ON a.id = l.productId "
    "LEFT JOIN workUnit_history h ON a.id = h.productId "
    "WHERE a.id = ?", id
);
```

**Result:** 13x faster

### 3. Prepared Statements

```cpp
// Cache prepared statement
static auto stmt = db.prepare("SELECT * FROM product WHERE id = ?");
stmt.bind(1, productId);
auto result = stmt.execute();
```

**Result:** 5x faster than re-parsing SQL each time

---

## Design Decisions

### Why GTK4 instead of Qt?

- **Native Linux look-and-feel** - integrates seamlessly with Ubuntu
- **CSS theming** - flexible styling without recompilation
- **Smaller binary size** - important for embedded targets
- **No licensing concerns** - pure open source
- **Industry standard** - widely used in industrial HMI

### Why Boost::Signals2 instead of Qt signals?

- **Framework agnostic** - doesn't tie us to Qt
- **Thread-safe** - works across threads without queueing
- **Header-only** - no additional runtime dependencies
- **Familiar** - standard C++ idioms

### Why SQLite instead of PostgreSQL?

- **Embedded** - no separate database server to manage
- **Single file** - easy backup and deployment
- **WAL mode** - concurrent reads while writing
- **Sufficient performance** - handles our data volume easily

---

## Testing Strategy

### Unit Tests
- **Presenter layer**: Test ViewModels, state machine logic
- **Mock observers**: Verify correct notifications
- **Database layer**: Test queries, transactions

### Integration Tests
- **OPC-UA simulator**: Mock PLC responses
- **Full workflow tests**: Simulate production cycle

### Tools
- **GoogleTest**: Unit testing framework
- **Valgrind**: Memory leak detection
- **ThreadSanitizer**: Race condition detection
- **Perf**: Performance profiling

---

## Future Improvements

1. **Coroutines (C++20)**: Simplify async code
2. **Boost.MSM**: State machine library instead of manual switch/case
3. **Structured logging (spdlog)**: Better debugging
4. **More automated tests**: Increase coverage beyond 85%

---

**Author:** Bogdan Baloi  
**Last Updated:** April 2026
