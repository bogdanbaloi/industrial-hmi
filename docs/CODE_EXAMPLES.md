# Generic Code Examples

> **Note:** All code examples use generic terminology to avoid domain-specific references.

## Database Query Optimization Example

### Before Optimization (Multiple Queries)

```cpp
// ❌ SLOW: 3 separate queries (200ms total)
auto product = db.query("SELECT * FROM products WHERE id=?", id);
auto specs = db.query("SELECT * FROM specifications WHERE productId=?", id);
auto history = db.query("SELECT * FROM processing_history WHERE productId=?", id);
```

### After Optimization (Single JOIN)

```cpp
// ✅ FAST: Single JOIN query (15ms)
auto result = db.query(
    "SELECT p.*, s.*, h.* "
    "FROM products p "
    "LEFT JOIN specifications s ON p.id = s.productId "
    "LEFT JOIN processing_history h ON p.id = h.productId "
    "WHERE p.id = ?",
    id
);
```

**Result:** 13x faster (200ms → 15ms)

---

## Prepared Statements Example

```cpp
// Cache prepared statement (compiled once, reused many times)
static auto stmt = db.prepare("SELECT * FROM products WHERE id = ?");
stmt.bind(1, productId);
auto result = stmt.execute();
```

**Result:** 5x faster than re-parsing SQL each time

---

## Thread-Safe Signal Example

```cpp
// Model emits signals from hardware thread
boost::signals2::signal<void(std::string)> workUnitSignal;

// Presenter subscribes from any thread (thread-safe)
workUnitSignal.connect([this](std::string id) {
    handleWorkUnit(id);
});
```

---

## State Machine Example

```cpp
ControlPanelViewModel computeState() {
    ControlPanelViewModel vm;
    auto state = Main::getState();
    bool equipmentHome = checkEquipmentPositions();
    bool hasErrors = checkErrors();
    
    switch (state) {
        case IDLE:
            vm.startEnabled = equipmentHome && !hasErrors;
            vm.stopEnabled = false;
            vm.resetEnabled = true;
            vm.calibrationEnabled = equipmentHome;
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
            vm.resetEnabled = true;
            vm.calibrationEnabled = false;
            break;
    }
    return vm;
}
```

---

## ViewModel Example

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

## Data Flow Example

```
1. PLC sends "equipment status update" via OPC-UA
                    ↓
2. HardwareController receives on hardware thread
   → Parses raw data
   → Emits equipmentStatusChanged(id, status) signal
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
   → Calls observer->onEquipmentCardChanged(vm)
   → View updates widget: statusOperation->set_text(vm.status)
```
