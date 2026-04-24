# View Layer Documentation

## Overview

The View layer in our MVP architecture is responsible for **rendering** and **user interaction**. Views are **dumb** - they contain NO business logic, only presentation logic.

---

## Key Principles

### 1. Views Observe Presenters

```cpp
class DashboardPage : public Gtk::Box, public ViewObserver {
    // Implements ViewObserver interface
    void onWorkUnitChanged(const WorkUnitViewModel& vm) override {
        // Render ViewModel into GTK widgets
    }
};
```

### 2. Thread-Safe UI Updates

**Problem:** GTK is single-threaded. ViewObserver callbacks arrive on Presenter thread.

**Solution:** Use `Glib::signal_idle()` to marshal to GTK main thread:

```cpp
void DashboardPage::onEquipmentCardChanged(const EquipmentCardViewModel& vm) {
    // WRONG - called from Presenter thread!
    // equipmentOperation->set_text(vm.statusMessage);  // CRASH!
    
    // CORRECT - marshal to GTK thread
    Glib::signal_idle().connect_once([this, vm]() {
        // This lambda runs on GTK main thread - safe to update widgets
        equipmentOperation->set_text(vm.statusMessage);
    });
}
```

### 3. User Actions -> Presenter

```cpp
void DashboardPage::onStartButtonClicked() {
    // Forward to Presenter - NO business logic here!
    if (presenter_) {
        presenter_->onStartClicked();
    }
}
```

---

## Complete Data Flow Example

Let's trace a complete flow from hardware -> UI update:

```
1. PLC sends "equipment status changed" via OPC-UA
                    ↓
2. OpcuaController (hardware thread)
   -> Receives raw data
   -> Emits equipmentStatusChanged(id, rawStatus) signal
                    ↓
3. DashboardPresenter (presenter thread)
   -> Receives signal: handleEquipmentStatusUpdate(id, rawStatus)
   -> Queries database for additional info
   -> Builds ViewModel: buildEquipmentVM(id, rawStatus)
   -> Checks cache: if (vm != lastVM)
   -> Calls: notifyEquipmentCardChanged(vm)
                    ↓
4. BasePresenter::notifyAll()
   -> Iterates all registered observers
   -> Calls: observer->onEquipmentCardChanged(vm)
                    ↓
5. DashboardPage (presenter thread - UNSAFE!)
   -> onEquipmentCardChanged(vm) called
   -> Marshals to GTK thread: Glib::signal_idle().connect_once(...)
                    ↓
6. GTK main thread
   -> Lambda executes on GTK thread
   -> Safe to call: equipmentOperation->set_text(vm.statusMessage)
   -> UI updates!
```

**Key insight:** Data crosses **3 thread boundaries** safely using signals and Glib::signal_idle().

---

## View Construction Pattern

### Step 1: Inherit from GTK widget + ViewObserver

```cpp
class DashboardPage : public Gtk::Box,      // GTK widget
                      public ViewObserver { // Observer interface
public:
    DashboardPage();
    void initialize(std::shared_ptr<DashboardPresenter> presenter);
    
    // ViewObserver interface
    void onWorkUnitChanged(const WorkUnitViewModel& vm) override;
    void onEquipmentCardChanged(const EquipmentCardViewModel& vm) override;
    // ... more observer methods
};
```

### Step 2: Build UI in constructor

```cpp
DashboardPage::DashboardPage() : Gtk::Box(Gtk::Orientation::VERTICAL) {
    buildUI();        // Create all GTK widgets
    applyStyles();    // Load CSS
}

void DashboardPage::buildUI() {
    // Create widgets
    workUnitOperation_ = Gtk::make_managed<Gtk::Operation>("-");
    progressBar_ = Gtk::make_managed<Gtk::ProgressBar>();
    
    // Build layout
    append(*workUnitOperation_);
    append(*progressBar_);
}
```

### Step 3: Register with Presenter

```cpp
void DashboardPage::initialize(std::shared_ptr<DashboardPresenter> presenter) {
    presenter_ = presenter;
    
    // Register as observer - will receive all updates
    presenter_->addObserver(this);
}

DashboardPage::~DashboardPage() {
    // IMPORTANT: Unregister before destruction!
    if (presenter_) {
        presenter_->removeObserver(this);
    }
}
```

### Step 4: Implement Observer callbacks

```cpp
void DashboardPage::onWorkUnitChanged(const WorkUnitViewModel& vm) {
    // Marshal to GTK thread
    Glib::signal_idle().connect_once([this, vm]() {
        updateWorkUnitWidgets(vm);  // Safe on GTK thread
    });
}

void DashboardPage::updateWorkUnitWidgets(const WorkUnitViewModel& vm) {
    // Render ViewModel into widgets
    workUnitOperation_->set_text(vm.workUnitId);
    progressBar_->set_fraction(vm.progress);
}
```

### Step 5: Forward user actions

```cpp
void DashboardPage::buildControlPanel() {
    auto* startButton = Gtk::make_managed<Gtk::Button>("START");
    
    // Connect button signal to handler
    startButton->signal_clicked().connect(
        sigc::mem_fun(*this, &DashboardPage::onStartButtonClicked)
    );
}

void DashboardPage::onStartButtonClicked() {
    // Forward to Presenter - NO business logic!
    if (presenter_) {
        presenter_->onStartClicked();
    }
}
```

---

## Widget Organization

Organize widgets by UI section for clarity:

```cpp
class DashboardPage : public Gtk::Box, public ViewObserver {
private:
    // Group related widgets into structs
    struct WorkUnitWidgets {
        Gtk::Operation* workUnitIdOperation{nullptr};
        Gtk::Operation* productIdOperation{nullptr};
        Gtk::ProgressBar* progressBar{nullptr};
        Gtk::Operation* statusOperation{nullptr};
    } workUnitWidgets_;
    
    struct ControlPanelWidgets {
        Gtk::Button* startButton{nullptr};
        Gtk::Button* stopButton{nullptr};
        Gtk::Button* resetButton{nullptr};
    } controlPanelWidgets_;
    
    // Collections for dynamic widgets
    std::vector<EquipmentCard> equipmentCards_;
    
    // Presenter reference
    std::shared_ptr<DashboardPresenter> presenter_;
};
```

---

## CSS Styling

### Option 1: Inline CSS (good for demos)

```cpp
void DashboardPage::applyStyles() {
    auto cssProvider = Gtk::CssProvider::create();
    
    cssProvider->load_from_data(R"(
        .equipment-card {
            border: 1px solid #ccc;
            border-radius: 8px;
            padding: 15px;
        }
        
        .status-operation {
            font-weight: bold;
            font-size: 16px;
        }
    )");
    
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(),
        cssProvider,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
}
```

### Option 2: External CSS file (production)

```cpp
void DashboardPage::applyStyles() {
    auto cssProvider = Gtk::CssProvider::create();
    cssProvider->load_from_path("assets/css/dashboard.css");
    
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(),
        cssProvider,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
}
```

---

## Common Patterns

### Pattern 1: Dynamic Card Lists

```cpp
// Create placeholder cards
for (uint32_t i = 0; i < numEquipment; ++i) {
    EquipmentCard card;
    card.equipmentId = i + 1;
    card.cardBox = Gtk::make_managed<Gtk::Box>(...);
    card.statusOperation = Gtk::make_managed<Gtk::Operation>("Offline");
    // ... create widgets
    
    equipmentCards_.push_back(card);
}

// Update specific card when ViewModel arrives
void DashboardPage::updateEquipmentCard(const EquipmentCardViewModel& vm) {
    // Find card by ID
    auto it = std::find_if(equipmentCards_.begin(), equipmentCards_.end(),
        [&vm](const EquipmentCard& card) { 
            return card.equipmentId == vm.equipmentId; 
        }
    );
    
    if (it != equipmentCards_.end()) {
        it->statusOperation->set_text(vm.statusMessage);
        it->statusImage->set_from_file(vm.imagePath);
    }
}
```

### Pattern 2: Conditional Visibility

```cpp
void DashboardPage::updateStatusZone(const StatusZoneViewModel& vm) {
    if (vm.severity == StatusZoneViewModel::Severity::NONE) {
        statusBanner_->set_visible(false);  // Hide banner
        return;
    }
    
    statusBanner_->set_visible(true);
    messageOperation_->set_text(vm.message);
    
    // Apply CSS class based on severity
    statusBanner_->remove_css_class("severity-info");
    statusBanner_->remove_css_class("severity-warning");
    statusBanner_->remove_css_class("severity-error");
    
    switch (vm.severity) {
        case Severity::ERROR:
            statusBanner_->add_css_class("severity-error");
            break;
        // ... handle other cases
    }
}
```

### Pattern 3: Button State from ViewModel

```cpp
void DashboardPage::updateControlPanel(const ControlPanelViewModel& vm) {
    // ViewModel tells us which buttons should be enabled
    startButton_->set_sensitive(vm.startEnabled);
    stopButton_->set_sensitive(vm.stopEnabled);
    resetButton_->set_sensitive(vm.resetRestartEnabled);
    
    // Visual indicator of active operation
    switch (vm.activeButton) {
        case ActiveControl::Start:
            activeIndicator_->set_text("RUNNING");
            break;
        case ActiveControl::Stop:
            activeIndicator_->set_text("STOPPED");
            break;
        default:
            activeIndicator_->set_text("IDLE");
            break;
    }
}
```

---

## Testing Views

### Unit Testing Approach

Views are hard to unit test because they depend on GTK. Instead:

1. **Test Presenters** - they contain all the logic
2. **Mock Observers** - verify correct ViewModels are emitted
3. **Manual Testing** - actually run the UI

```cpp
// Test Presenter, not View
TEST(DashboardPresenter, NotifiesWorkUnitChanged) {
    DashboardPresenter presenter;
    MockObserver mockObserver;
    presenter.addObserver(&mockObserver);
    
    presenter.handleNewWorkUnit("WU-123");
    
    EXPECT_EQ(mockObserver.lastWorkUnitVM.workUnitId, "WU-123");
}
```

---

## Best Practices

### DO:

- Keep Views dumb - only rendering logic
- Use Glib::signal_idle() for thread safety
- Forward user actions immediately to Presenter
- Group related widgets into structs
- Apply CSS for styling, not hardcoded colors
- Unregister from Presenter in destructor

### DON'T:

- Put business logic in Views
- Access GTK widgets from non-GTK threads
- Query database from Views
- Implement state machines in Views
- Duplicate logic between Views and Presenters
- Forget to unregister observers

---

## File Organization

```
src/gtk/view/
├── pages/
│   ├── DashboardPage.h
│   ├── DashboardPage.cpp
│   ├── ProductsPage.h
│   └── ProductsPage.cpp
├── dialogs/
│   ├── ErrorDialog.h
│   ├── ConfigDialog.h
│   └── ...
└── widgets/
    ├── EquipmentCard.h
    ├── ActuatorCard.h
    └── StatusBanner.h
```

---

## See Also

- `docs/ARCHITECTURE.md` - Complete MVP architecture explanation
- `src/presenter/ViewObserver.h` - Observer interface documentation
- `src/presenter/BasePresenter.h` - Presenter base class

---

**Remember:** Views are **renderers**, not **thinkers**. All intelligence lives in the Presenter!
