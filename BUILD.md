# Building Industrial HMI - Cross-Platform Guide

**This application builds on both Linux and Windows!** 🐧🪟

---

## 🐧 **Linux Build (Ubuntu/Debian)**

### Prerequisites

```bash
# Ubuntu 24.04 / Debian 12+
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    libgtkmm-4.0-dev \
    libsqlite3-dev \
    libboost-dev
```

### Build Steps

```bash
# Clone repository
git clone <repository-url>
cd industrial-hmi-portfolio

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..

# Build
ninja

# Run
./industrial-hmi
```

---

## 🪟 **Windows Build (Windows 10/11)**

### Prerequisites

1. **Install Visual Studio 2022**
   - Download: https://visualstudio.microsoft.com/downloads/
   - Workload: "Desktop development with C++"

2. **Install vcpkg** (Package Manager)
   ```powershell
   git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
   cd C:\vcpkg
   .\bootstrap-vcpkg.bat
   ```

3. **Install Dependencies**
   ```powershell
   vcpkg install gtkmm:x64-windows
   vcpkg install sqlite3:x64-windows
   vcpkg install boost-signals2:x64-windows
   ```

### Build Steps

```powershell
# Configure
cmake -G "Visual Studio 17 2022" `
    -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\vcpkg.cmake" `
    ..

# Build
cmake --build . --config Release

# Run
.\Release\industrial-hmi.exe
```

---

## 📦 **Pre-Built Binaries**

**GitHub Releases:**
- Linux: `industrial-hmi-ubuntu.tar.gz`
- Windows: `industrial-hmi-windows.zip`

---

**Cross-platform development made easy!** 🚀
