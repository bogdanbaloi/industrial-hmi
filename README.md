# Industrial HMI - MVP Architecture Portfolio

**Modern Industrial Control System** demonstrating professional MVP architecture patterns with clean separation of concerns and thread-safe UI updates.

> 🎯 **Portfolio Focus**: Architecture patterns and implementation quality, not business domain specifics. All proprietary content anonymized.

---

## 🎨 What You'll See

### Dashboard (Tab 1)
Modern industrial monitoring interface with:

**Work Unit Section**
- Current production item tracking
- Progress visualization (operations completed)
- Product information display

**Equipment Stations** (4 monitoring cards)
- Real-time status indicators (colored status dots)
- Supply level monitoring
- Enable/disable controls
- 2×2 grid layout for quick overview

**Quality Checkpoints** (3 inspection points)
- Pass rate tracking (target: 95%+)
- Defect detection statistics
- Last defect information
- Business value: Quality = cost reduction

**Control Panel**
- START / STOP / RESET / CALIBRATION
- State-driven button enabling
- Clear visual feedback

### Products Database (Tab 2)
Data management interface demonstrating:
- Database integration (SQLite)
- Search/filter functionality
- TreeView list presentation
- Detail view dialogs
- 9 sample products with realistic data

---

## 🏗️ Architecture Highlights

### MVP Pattern (Model-View-Presenter)
```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│   Model     │────────▶│  Presenter   │────────▶│    View     │
│             │         │              │         │   (GTK4)    │
│ SimulatedModel      │ Transforms    │         │             │
│ DatabaseManager     │ to ViewModels │         │ DashboardPage
│             │         │              │         │ ProductsPage│
└─────────────┘         └──────────────┘         └─────────────┘
                              ▲                         │
                              └─────────────────────────┘
                                   User Actions
```

**Model Layer**: Data and business state (simulated for demo)
**Presenter Layer**: Orchestrates data flow, transforms to ViewModels
**View Layer**: Pure rendering, GTK4 widgets, forwards user actions

### Observer Pattern
Views observe Presenters via `ViewObserver` interface:
- Presenter notifies observers of state changes
- Views receive ViewModels (DTOs)
- Thread-safe updates via `Glib::signal_idle()`

### Thread Safety
```cpp
// Model callbacks arrive on background threads
model.onEquipmentStatusChanged([this](const auto& status) {
    auto vm = buildViewModel(status);
    
    // Marshal to GTK main thread
    Glib::signal_idle().connect_once([this, vm]() {
        updateUI(vm);
    });
});
```

---

## 📊 What This Demonstrates

**For Senior Software Engineer Interviews:**

✅ **Architecture**: Clean MVP separation, no business logic in View  
✅ **Design Patterns**: Observer, DTO/ViewModel, Dependency Injection  
✅ **Threading**: Thread-safe UI updates, background processing  
✅ **Database**: SQLite integration, prepared statements, query optimization  
✅ **UI/UX**: Modern industrial design, GTK4, CSS styling  
✅ **Code Quality**: SOLID principles, clear naming, documentation  
✅ **Real-world Skills**: Multi-view apps, state machines, data transformation  

**Business Value Focus:**
- Quality Checkpoints: Defect tracking → cost reduction
- Equipment Monitoring: Downtime prevention → productivity
- Work Unit Tracking: Progress visibility → delivery predictability

---

## 🚀 Build & Run

### Prerequisites

**Ubuntu/Debian:**
```bash
sudo apt-get install -y build-essential cmake \
    libgtkmm-4.0-dev libboost-dev libsqlite3-dev
```

**Fedora/RHEL:**
```bash
sudo dnf install -y gcc-c++ cmake \
    gtkmm4.0-devel boost-devel sqlite-devel
```

**macOS:**
```bash
brew install cmake gtkmm4 boost sqlite3
```

### Build Steps

```bash
# 1. Clone repository
git clone https://github.com/YOUR-USERNAME/industrial-hmi-architecture.git
cd industrial-hmi-architecture

# 2. Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# 3. Run
./industrial-hmi
```

---

## 🎯 Demo Interaction

### Dashboard Tab
1. **START** → Advances production (3/5 → 4/5 operations)
2. **STOP** → Returns to IDLE state
3. **RESET** → Clears progress back to 0/5
4. **Equipment toggles** → Enable/disable stations
5. **Quality cards** → View pass rates and defect stats

### Products Tab
1. **Search** → Filter by product ID or name
2. **Double-click row** → View product details
3. **Refresh** → Reload from database

**Note**: Simulation is intentionally simplified to focus on architecture demonstration rather than complex production logic.

---

## 📁 Project Structure

```
industrial-hmi/
├── src/
│   ├── main.cpp                      # Application entry point
│   ├── model/
│   │   ├── SimulatedModel.h          # Demo data provider
│   │   └── DatabaseManager.h         # SQLite integration
│   ├── presenter/
│   │   ├── BasePresenter.h/cpp       # Observer management
│   │   ├── DashboardPresenter.h/cpp  # Dashboard orchestration
│   │   ├── ProductsPresenter.h/cpp   # Products data operations
│   │   ├── ViewObserver.h            # Observer interface
│   │   └── modelview/                # ViewModels (DTOs)
│   │       ├── EquipmentCardViewModel.h
│   │       ├── QualityCheckpointViewModel.h
│   │       ├── WorkUnitViewModel.h
│   │       └── ...
│   └── gtk/view/pages/
│       ├── DashboardPage.h/cpp       # Main monitoring UI
│       └── ProductsPage.h/cpp        # Database list view
├── docs/
│   ├── ARCHITECTURE.md               # Deep dive on design decisions
│   ├── VIEW_LAYER.md                 # View implementation details
│   └── CODE_EXAMPLES.md              # Pattern examples
├── BUILD.md                          # Complete build instructions
├── CMakeLists.txt                    # Build configuration
└── README.md                         # This file
```

---

## 🎨 Design Principles

**Modern Industrial Clean** (optimized for 1920×1080):

- **Color Palette**: Blue-gray tones (#263238, #546E7A, #90A4AE)
- **Typography**: Clear hierarchy (13-36px range, weights 500-700)
- **Spacing**: Generous (40-60px margins, airy layout)
- **Cards**: Subtle shadows, 12px radius, 2px borders
- **Status Indicators**: Colored dots (●) - no emoji, professional
- **Interactions**: Smooth transitions (0.2s), hover effects

---

## 💬 Interview Talking Points

### Architecture Decision: Why MVP?
> "MVP provides clear separation between UI rendering and business logic. This was critical for an industrial system where business rules change frequently but UI patterns remain stable. Presenters can be unit-tested independently of GTK."

### Thread Safety: Why `signal_idle()`?
> "Model callbacks arrive on hardware monitoring threads. GTK widgets can only be updated from the main thread. `signal_idle()` safely marshals updates to the GTK thread, preventing race conditions and crashes."

### ViewModels: Why DTOs?
> "ViewModels decouple Model data structures from View rendering. When the database schema changed, only Presenter code needed updates - Views remained unchanged. This flexibility was essential for evolving requirements."

### Quality Checkpoints: Business Value
> "Quality tracking isn't just monitoring - it's cost reduction. These 85 defects caught today prevented warranty claims and customer returns. That's why pass rates are prominent in the UI."

---

## 📝 License

MIT License - free for portfolio use

---

## 🔗 Related Documentation

- **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** - Threading model, data flow, design decisions
- **[VIEW_LAYER.md](docs/VIEW_LAYER.md)** - GTK4 patterns, thread safety, widget organization
- **[CODE_EXAMPLES.md](docs/CODE_EXAMPLES.md)** - Reusable patterns from this project
- **[BUILD.md](BUILD.md)** - Platform-specific build instructions

---

**Questions?** Open an issue or reach out for clarification on any architectural decisions.

**Note**: Due to NDA, specific business domain details are intentionally generic. Focus is on demonstrating architecture patterns and implementation quality.
