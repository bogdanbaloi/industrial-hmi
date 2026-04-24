# VSCode Commands for Industrial HMI

## Quick Start

### 1. Deschide Proiectul
```
File > Open Folder > Select: industrial-hmi/
```

### 2. Selectează Kit (La prima deschidere)
**Command Palette** (`Ctrl+Shift+P`):
```
CMake: Select a Kit
-> Alege: "MSYS2 CLANG64"
```

### 3. Build
**Shortcut:** `Ctrl+Shift+B`
**SAU Command Palette:**
```
CMake: Build
```

### 4. Run
**Shortcut:** `F5`
**SAU Command Palette:**
```
Debug: Start Debugging
```

---

## VSCode Commands (Ctrl+Shift+P)

### CMake Commands:
```
CMake: Configure              - Configurează CMake (generează build files)
CMake: Build                  - Compilează proiectul
CMake: Clean                  - Șterge build artifacts
CMake: Delete Cache           - Șterge CMake cache
CMake: Select a Kit           - Schimbă compiler kit
CMake: Select Variant         - Debug/Release
```

### Build Commands:
```
Tasks: Run Build Task         - Ctrl+Shift+B (default build)
Tasks: Run Task               - Alege task specific
```

### Debug Commands:
```
Debug: Start Debugging        - F5 (build + debug)
Debug: Start Without Debug    - Ctrl+F5 (run fără debug)
Debug: Toggle Breakpoint      - F9
Debug: Step Over              - F10
Debug: Step Into              - F11
Debug: Continue               - F5
```

### Terminal Commands:
```
Terminal: Create New Terminal - Ctrl+Shift+` (MSYS2 bash)
```

---

## Keyboard Shortcuts

| Action | Shortcut |
|--------|----------|
| Build | `Ctrl+Shift+B` |
| Debug | `F5` |
| Run (no debug) | `Ctrl+F5` |
| Command Palette | `Ctrl+Shift+P` |
| Toggle Terminal | `Ctrl+\`` |
| Toggle Breakpoint | `F9` |
| Step Over | `F10` |
| Step Into | `F11` |
| Go to Definition | `F12` |
| Find References | `Shift+F12` |
| Format Document | `Shift+Alt+F` |

---

## First Time Setup

### 1. Instalează GTK4 în MSYS2:
```bash
# Deschide MSYS2 CLANG64 terminal
pacman -S mingw-w64-clang-x86_64-gtk4
pacman -S mingw-w64-clang-x86_64-gtkmm-4.0
pacman -S mingw-w64-clang-x86_64-boost
pacman -S mingw-w64-clang-x86_64-sqlite3
pacman -S mingw-w64-clang-x86_64-cmake
pacman -S mingw-w64-clang-x86_64-ninja
```

### 2. Instalează VSCode Extensions:
```
C/C++ (Microsoft)
CMake Tools (Microsoft)
CMake (twxs)
```

### 3. Configurare Preset:
- Deschide proiectul în VSCode
- `Ctrl+Shift+P` -> `CMake: Select a Kit`
- Alege: **MSYS2 CLANG64**

---

## Debugging Tips

### Set Breakpoint:
- Click în margin (lângă numărul liniei)
- SAU `F9` pe linie

### Debug Variables:
- Hover peste variabilă
- SAU vezi în **VARIABLES** panel (stânga)

### Debug Console:
- Vezi output în **DEBUG CONSOLE**
- Poți evalua expresii: `p variableName`

### Call Stack:
- Vezi în **CALL STACK** panel
- Click pentru a sări la frame

---

## Status Bar (jos în VSCode)

```
[CMake Kit] [Build Type] [Build] [Debug] [Test]
     ↓           ↓          ↓       ↓      ↓
  CLANG64      Debug     Build   Debug  Test
```

**Click pe:**
- **Kit** -> Schimbă compiler
- **Build Type** -> Debug/Release
- **Build** -> Build rapid
- **Debug** -> Start debugging

---

## Common Workflows

### Workflow 1: Edit -> Build -> Run
```
1. Edit code
2. Ctrl+Shift+B (build)
3. F5 (debug/run)
```

### Workflow 2: Clean Build
```
1. Ctrl+Shift+P -> CMake: Delete Cache
2. Ctrl+Shift+P -> CMake: Configure
3. Ctrl+Shift+B -> Build
```

### Workflow 3: Quick Fix Compile Error
```
1. Ctrl+Shift+B (build)
2. Click pe error în PROBLEMS panel
3. Fix code
4. Ctrl+Shift+B (rebuild)
```

---

## IntelliSense Features

### Go to Definition:
- `F12` SAU `Ctrl+Click`

### Find All References:
- `Shift+F12`

### Peek Definition:
- `Alt+F12`

### Auto-complete:
- `Ctrl+Space`

### Parameter Hints:
- `Ctrl+Shift+Space`

---

## Troubleshooting

### "Kit not found"
**Solution:**
```
Ctrl+Shift+P -> CMake: Scan for Kits
-> Alege MSYS2 CLANG64
```

### "GTK4 not found"
**Solution:**
```bash
# MSYS2 terminal:
pacman -S mingw-w64-clang-x86_64-gtkmm-4.0
```

### IntelliSense errors dar compilează OK
**Solution:**
```
Ctrl+Shift+P -> C/C++: Reset IntelliSense Database
```

### Build fails cu "Ninja not found"
**Solution:**
```bash
# MSYS2 terminal:
pacman -S mingw-w64-clang-x86_64-ninja
```

---

## Project Structure în VSCode

```
INDUSTRIAL-HMI/
├── .vscode/              ← VSCode configurations
│   ├── settings.json     ← Editor & CMake settings
│   ├── c_cpp_properties.json  ← IntelliSense paths
│   ├── tasks.json        ← Build tasks
│   ├── launch.json       ← Debug configs
│   └── cmake-kits.json   ← Compiler kits
├── src/                  ← Cod sursă (explorer stânga)
├── build/                ← Build output (ignored)
└── CMakeLists.txt        ← CMake config
```

---

## Pro Tips

1. **Multi-cursor editing:** `Alt+Click`
2. **Duplicate line:** `Shift+Alt+Down`
3. **Comment line:** `Ctrl+/`
4. **Format on save:** Settings -> Editor: Format On Save
5. **Zen mode:** `Ctrl+K Z` (fullscreen coding)

---

**Enjoy coding!**
