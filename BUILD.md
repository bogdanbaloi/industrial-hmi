# Build Instructions

## Prerequisites

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libgtkmm-4.0-dev \
    libboost-dev \
    libsqlite3-dev \
    pkg-config
```

### Fedora/RHEL
```bash
sudo dnf install -y \
    gcc-c++ \
    cmake \
    gtkmm4.0-devel \
    boost-devel \
    sqlite-devel \
    pkgconfig
```

### macOS (Homebrew)
```bash
brew install cmake gtkmm4 boost sqlite3 pkg-config
```

---

## Build Steps

### 1. Clone Repository
```bash
git clone https://github.com/YOUR-USERNAME/industrial-hmi-architecture.git
cd industrial-hmi-architecture
```

### 2. Create Build Directory
```bash
mkdir build
cd build
```

### 3. Configure with CMake
```bash
cmake ..
```

### 4. Compile
```bash
make -j$(nproc)
```

### 5. Run
```bash
./industrial-hmi
```

---

## What You'll See

The application will launch with a GTK4 window showing:

- **Work Unit Section**: Simulated production progress (3/5 operations complete)
- **Equipment Cards**: 4 equipment stations with different statuses
  - Equipment 1: Online (green, 85% supply)
  - Equipment 2: Processing (orange, working)
  - Equipment 3: Error (red, low supply)
  - Equipment 4: Offline (gray)
- **Actuator Cards**: 2 automated actuators
  - Actuator 0: Working (blue, at position X:150 Y:200)
  - Actuator 1: Idle (green, at home position)
- **Control Panel**: Buttons for START, STOP, RESET, CALIBRATION

### Interactive Demo

Try clicking:
- **START** button → System starts "production"
- **STOP** button → System returns to IDLE
- **RESET** button → Resets work unit progress
- **Equipment toggles** → Enable/disable equipment

---

## Troubleshooting

### GTK4 not found
```
CMake Error: Could not find gtkmm-4.0
```
**Solution:** Install GTK4 development packages (see Prerequisites)

### Boost not found
```
CMake Error: Could not find Boost
```
**Solution:** Install boost-devel or libboost-dev

### Compilation errors about "Label"
This is an older version - pull latest from git:
```bash
git pull origin main
```

---

## Development Build

For development with debug symbols:
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
```

For release build with optimizations:
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

---

## What This Demonstrates

This application is a **functional MVP architecture demo** showing:

1. **Model Layer**: Simulated data (see `src/model/SimulatedModel.h`)
2. **Presenter Layer**: Business logic orchestration (see `src/presenter/`)
3. **View Layer**: GTK4 UI rendering (see `src/gtk/view/`)
4. **Observer Pattern**: View observes Presenter via `ViewObserver` interface
5. **Thread Safety**: UI updates marshaled with `Glib::signal_idle()`
6. **ViewModels**: DTOs for clean data flow
7. **Professional Assets**: Technical SVG graphics

---

## Architecture Highlights

**Data Flow:**
```
SimulatedModel → DashboardPresenter → ViewModels → DashboardPage → GTK Widgets
                     ↑                                    ↓
                     └─────── User Actions ──────────────┘
```

**Thread Safety:**
- Model callbacks arrive on background threads
- Presenter uses `Glib::signal_idle()` to marshal to GTK thread
- UI updates always on GTK main thread

**Design Patterns:**
- MVP (Model-View-Presenter)
- Observer (View observes Presenter)
- DTO (ViewModels)
- Dependency Injection (Presenter injected into View)

---

For architecture details, see `docs/ARCHITECTURE.md`
For View layer specifics, see `docs/VIEW_LAYER.md`
