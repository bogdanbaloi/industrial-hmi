# Industrial HMI - Portfolio Project

**A Modern C++20 Industrial Human-Machine Interface demonstrating professional software architecture patterns and pharmaceutical manufacturing domain knowledge.**

[![C++](https://img.shields.io/badge/C++-20-blue.svg?style=flat&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/20)
[![GTK](https://img.shields.io/badge/GTK-4-green.svg?style=flat)](https://www.gtk.org/)
[![License](https://img.shields.io/badge/license-MIT-blue.svg?style=flat)](LICENSE)

---

## 🎯 **Purpose**

This project showcases:
- **Software Architecture**: MVP pattern, SOLID principles, Dependency Injection
- **Modern C++**: C++20 features, Core Guidelines, design patterns
- **Domain Knowledge**: Pharmaceutical tablet manufacturing processes
- **Professional Practices**: Complete CRUD, error handling, testability

**Target audience**: Senior/Staff/Principal Software Engineer roles in industrial automation, embedded systems, or C++ development.

---

## 🏭 **Domain: Pharmaceutical Manufacturing**

### Production Lines (3 Equipment Stations)

| Line | Equipment | Capacity | Purpose |
|------|-----------|----------|---------|
| **A-LINE** | Tablet Press | 85,000 tablets/hr | Compression molding |
| **B-LINE** | Film Coater | 45μm @ 12 RPM | Protective coating |
| **C-LINE** | Blister Pack | 120 packs/min | Packaging |

### Quality Checkpoints

- **Weight Check**: ±5% tolerance
- **Hardness Test**: 80-120 N force
- **Final Inspection**: Visual + dimensional

### Work Unit Tracking

- Format: `Batch WU-2024-001234 | TAB-200`
- Simplified internal codes (NDA-compliant)
- No chemical formulas or proprietary data

---

## 🏗️ **Architecture**

### MVP (Model-View-Presenter) Pattern

```
┌─────────────────┐
│  View (GTK4)    │  ← Pure UI, no business logic
│  - DashboardPage│
│  - ProductsPage │
└────────┬────────┘
         │ ViewObserver
         │ interface
┌────────▼────────┐
│  Presenter      │  ← Business logic coordinator
│  - Dashboard    │
│  - Products     │
└────────┬────────┘
         │
┌────────▼────────┐
│  Model          │  ← Data & domain logic
│  - Database     │
│  - Simulated    │
└─────────────────┘
```

### Key Design Patterns

| Pattern | Usage | Benefits |
|---------|-------|----------|
| **Dependency Injection** | DialogManager, Services | Testable, explicit dependencies |
| **Observer** | View ← Presenter updates | Decoupled communication |
| **Factory** | Dialog creation | Consistent UI components |
| **Template Method** | BasePresenter&lt;View&gt; | Code reuse with type safety |
| **RAII** | Resource management | No memory leaks |

---

## ✨ **Features**

### ✅ **Complete CRUD Operations**

- **CREATE**: Add new products with validation
- **READ**: View product list, search, details
- **UPDATE**: Edit existing products (immutable codes)
- **DELETE**: Soft delete (audit trail preserved)

### ✅ **Soft Delete Pattern**

```sql
-- Products never truly deleted
UPDATE products 
SET deleted_at = CURRENT_TIMESTAMP 
WHERE id = ?

-- Queries exclude deleted
SELECT * FROM products 
WHERE deleted_at IS NULL
```

**Benefits**: Audit trail, data recovery, compliance

### ✅ **Dependency Injection**

**Before (Singleton - Anti-Pattern)**:
```cpp
void onError() {
    DialogManager::instance().showError(...);  // ❌ Hidden dependency
}
```

**After (DI - Production Pattern)**:
```cpp
class ProductsPage {
    DialogManager& dialogManager_;  // ✅ Explicit dependency
public:
    ProductsPage(DialogManager& dm) : dialogManager_(dm) {}
};
```

**Result**: Testable, clear dependencies, no global state

### ✅ **Modern C++20**

- **Concepts**: `ViewInterfaceConcept` for compile-time validation
- **Ranges**: Cleaner container operations
- **Attributes**: `[[nodiscard]]`, `[[likely]]`
- **constexpr**: Compile-time evaluation
- **Core Guidelines**: CP.20, Con.1, Con.3, ES.25

### ✅ **Thread-Safe Design**

- DialogManager auto-marshals to GTK thread
- Scoped locks (`std::scoped_lock` > `std::lock_guard`)
- Const correctness throughout

---

## 🛠️ **Technology Stack**

| Component | Technology | Version |
|-----------|-----------|---------|
| Language | C++ | 20 |
| UI Framework | GTK | 4.x |
| Build System | CMake + Ninja | 3.20+ |
| Database | SQLite3 | 3.x |
| Platform | Linux (Ubuntu 24) | - |

---

## 📦 **Project Structure**

```
industrial-hmi/
├── src/
│   ├── main.cpp                    # Entry point (3 lines!)
│   ├── gtk/view/
│   │   ├── MainWindow.h/.cpp       # Main window, owns services
│   │   ├── DialogManager.h/.cpp    # Centralized dialogs (DI)
│   │   └── pages/
│   │       ├── DashboardPage.h/.cpp    # Equipment monitoring
│   │       └── ProductsPage.h/.cpp     # CRUD interface
│   ├── presenter/
│   │   ├── DashboardView.h         # ISP interface
│   │   ├── ProductsView.h          # ISP interface  
│   │   ├── BasePresenterTemplate.h # C++20 concepts
│   │   ├── DashboardPresenter.h/.cpp
│   │   └── ProductsPresenter.h/.cpp
│   └── model/
│       ├── SimulatedModel.h        # 3 equipment demo data
│       └── DatabaseManager.h       # CRUD + soft delete
├── ui/
│   ├── main-window.ui              # GTK layout
│   └── sidebar.css                 # Industrial dark theme
├── tests/
│   └── README_TESTING.md           # Unit test examples
├── CMakeLists.txt
└── README.md
```

---

## 🚀 **Building & Running**

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake ninja-build \
                 libgtkmm-4.0-dev libsqlite3-dev

# Fedora/RHEL
sudo dnf install gcc-c++ cmake ninja-build \
                 gtkmm4.0-devel sqlite-devel
```

### Build

```bash
mkdir build && cd build
cmake -G Ninja ..
ninja

# Run
./industrial-hmi
```

### Controls

| Key | Action |
|-----|--------|
| **F11** | Toggle fullscreen |
| **ESC** | Exit fullscreen |

---

## 📊 **Git History Highlights**

```bash
git log --oneline
```

Key commits demonstrating software engineering skills:

```
f5e8937 Implement Edit/Update - Complete CRUD
b33e985 Integrate DialogManager into DashboardPage with DI
2ac3bec Refactor DialogManager from Singleton to Dependency Injection  ← MAJOR
cd812e7 Add centralized DialogManager for consistent UI
14e971a Implement complete CRUD UI for Products page
ea0a51f Add CRUD operations with deleted_at soft delete
ea76cd4 Refactor to pharmaceutical manufacturing domain
c9e57d1 Apply C++ Core Guidelines: scoped_lock + const correctness
a0d1270 Upgrade to C++20 with Concepts and Modern Features
d613dee Add SOLID ISP interfaces with Modern C++ best practices
```

**Notable**: Commit `2ac3bec` shows refactoring from anti-pattern (Singleton) to production pattern (DI) - demonstrates critical thinking and pattern knowledge.

---

## 🎓 **Interview Talking Points**

### Architecture

> **"I implemented MVP architecture with complete separation of concerns. The View is pure GTK UI with no business logic. The Presenter coordinates between View and Model using the Observer pattern. This makes the codebase maintainable and testable."**

### Design Patterns

> **"I initially used a Singleton for DialogManager but recognized this as an anti-pattern. I refactored to Dependency Injection - MainWindow owns DialogManager and injects it into pages via constructor. This makes dependencies explicit and enables unit testing with mock objects."**

### SOLID Principles

> **"I applied Interface Segregation Principle - instead of one fat ViewObserver with 20+ methods, I created focused interfaces: DashboardView (4 methods), ProductsView (3 methods). This prevents classes from depending on methods they don't use."**

### C++20 Modernization

> **"I used C++20 concepts to enforce compile-time constraints. BasePresenter is a template that accepts only types satisfying ViewInterfaceConcept. This catches errors at compile time rather than runtime."**

### Domain Knowledge

> **"I modeled pharmaceutical tablet manufacturing: Tablet Press (85K tablets/hr), Film Coater (45μm @ 12 RPM), Blister Pack (120/min). Real factory floor naming like 'A-LINE' instead of academic 'Station 1'. This shows I understand both software AND the domain."**

### Database Patterns

> **"I implemented soft delete with `deleted_at` timestamp instead of hard delete. Benefits: audit trail, data recovery, compliance requirements. Queries use `WHERE deleted_at IS NULL` to exclude deleted records."**

### Testability

> **"Dependency Injection makes this codebase fully testable. I can inject MockDialogManager to verify UI behavior without showing actual dialogs. See `tests/README_TESTING.md` for examples."**

---

## 📚 **Additional Resources**

- **Testing Guide**: `tests/README_TESTING.md` - Unit test examples with DI
- **Git History**: 40+ commits showing incremental development
- **Code Comments**: Doxygen-style documentation throughout

---

## 🎯 **Skills Demonstrated**

### Software Engineering
✅ MVP architecture  
✅ SOLID principles  
✅ Design patterns (DI, Observer, Factory, Template Method)  
✅ Refactoring anti-patterns  
✅ Code review mindset  

### Modern C++
✅ C++20 concepts  
✅ Template metaprogramming  
✅ Core Guidelines compliance  
✅ Thread safety  
✅ RAII & smart pointers  

### Domain Knowledge
✅ Pharmaceutical manufacturing  
✅ Equipment monitoring  
✅ Quality control systems  
✅ Industrial HMI requirements  

### Professional Practices
✅ Complete CRUD operations  
✅ Error handling  
✅ Data integrity (soft delete, immutable codes)  
✅ Testability  
✅ Documentation  

---

## 📝 **License**

MIT License - Free to use for portfolio review and educational purposes.

---

## 👤 **Contact**

**Portfolio Project** - Demonstrating Senior/Staff/Principal Software Engineer capabilities

- **Focus**: Industrial C++, Embedded Systems, Automation
- **Skills**: Architecture, Patterns, Domain Knowledge, Modern C++

---

**Built with attention to production-quality code and professional software engineering practices.** ✨
