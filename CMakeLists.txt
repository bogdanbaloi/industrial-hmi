cmake_minimum_required(VERSION 3.20)
project(industrial-hmi 
    VERSION 1.0.0 
    DESCRIPTION "Industrial HMI MVP Architecture - Cross-Platform (Linux/Windows)" 
    LANGUAGES CXX)

# ============================================================================
# Platform Detection
# ============================================================================
if(WIN32)
    message(STATUS "🪟 Building for Windows")
    set(PLATFORM_WINDOWS TRUE)
elseif(UNIX AND NOT APPLE)
    message(STATUS "🐧 Building for Linux")
    set(PLATFORM_LINUX TRUE)
elseif(APPLE)
    message(STATUS "🍎 Building for macOS")
    set(PLATFORM_MACOS TRUE)
endif()

# ============================================================================
# C++20 Standard (Cross-Platform)
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Compiler Warnings (Platform-Specific)
# ============================================================================
if(MSVC)
    # MSVC (Visual Studio / Windows)
    add_compile_options(
        /W4                 # Warning level 4
        /permissive-        # Standards conformance
        /utf-8              # UTF-8 source files
        /EHsc               # Exception handling
    )
    # Disable specific warnings
    add_compile_options(
        /wd4251             # DLL interface warnings
        /wd4275             # DLL base class warnings
    )
else()
    # GCC/Clang (Linux/macOS)
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-unused-parameter
    )
endif()

# ============================================================================
# Sanitizers (Debug builds - GCC/Clang only)
# ============================================================================
option(ENABLE_SANITIZERS "Enable sanitizers (AddressSanitizer, UBSanitizer)" OFF)

if(ENABLE_SANITIZERS AND NOT MSVC)
    message(STATUS "Sanitizers enabled: AddressSanitizer + UndefinedBehaviorSanitizer")
    
    set(SANITIZER_FLAGS
        -fsanitize=address           # AddressSanitizer - detect memory errors
        -fsanitize=undefined         # UndefinedBehaviorSanitizer - detect UB
        -fno-omit-frame-pointer      # Better stack traces
        -g                           # Debug symbols
    )
    
    add_compile_options(${SANITIZER_FLAGS})
    add_link_options(${SANITIZER_FLAGS})
    
    # Optional: Thread sanitizer (conflicts with AddressSanitizer)
    # -fsanitize=thread
endif()

# ============================================================================
# Code Coverage (Debug builds - GCC/Clang only)
# ============================================================================
option(ENABLE_COVERAGE "Enable code coverage reporting" OFF)

if(ENABLE_COVERAGE AND NOT MSVC)
    message(STATUS "Code coverage enabled")
    
    set(COVERAGE_FLAGS
        --coverage              # Enable coverage
        -fprofile-arcs         # Generate .gcda files
        -ftest-coverage        # Generate .gcno files
    )
    
    add_compile_options(${COVERAGE_FLAGS})
    add_link_options(${COVERAGE_FLAGS})
endif()

# ============================================================================
# Platform-Specific Definitions
# ============================================================================
if(WIN32)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS     # Disable MSVC secure warnings
        NOMINMAX                     # Don't define min/max macros
        WIN32_LEAN_AND_MEAN         # Exclude rarely-used Windows headers
    )
endif()

# ============================================================================
# Find Dependencies (Cross-Platform)
# ============================================================================

# On Windows, use vcpkg for dependencies
# Set: -DCMAKE_TOOLCHAIN_FILE=[vcpkg]/scripts/buildsystems/vcpkg.cmake
if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(WARNING "⚠️  For Windows builds, use vcpkg toolchain file:")
    message(WARNING "    cmake -DCMAKE_TOOLCHAIN_FILE=[vcpkg]/scripts/buildsystems/vcpkg.cmake ..")
endif()

# GTK4 (cross-platform via pkg-config or vcpkg)
if(PLATFORM_LINUX OR PLATFORM_MACOS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTKMM REQUIRED gtkmm-4.0)
    pkg_check_modules(SQLITE REQUIRED sqlite3)
else()
    # Windows: Use vcpkg packages
    find_package(gtkmm REQUIRED)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
    set(GTKMM_LIBRARIES gtkmm::gtkmm)
    set(SQLITE_LIBRARIES unofficial::sqlite3::sqlite3)
endif()

# Boost (header-only signals2)
find_package(Boost REQUIRED)

# Threads (cross-platform)
find_package(Threads REQUIRED)

# ============================================================================
# Object Libraries (Modular Build Pattern)
# ============================================================================

# Model Layer - Data and business logic simulation
add_library(objectsModel OBJECT)
target_sources(objectsModel
    PRIVATE
        src/model/DatabaseManager.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/model/SimulatedModel.h
            src/model/DatabaseManager.h
            src/model/ModelContext.h
)

# Config Layer - JSON configuration management
add_library(objectsConfig INTERFACE)
target_sources(objectsConfig
    INTERFACE
        FILE_SET HEADERS
        FILES
            src/config/ConfigManager.h
)

# Presenter Layer - MVP orchestration
add_library(objectsPresenter OBJECT)
target_sources(objectsPresenter
    PRIVATE
        src/presenter/DashboardPresenter.cpp
        src/presenter/ProductsPresenter.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/presenter/BasePresenter.h
            src/presenter/DashboardPresenter.h
            src/presenter/ProductsPresenter.h
            src/presenter/ViewObserver.h
            src/presenter/modelview/EquipmentCardViewModel.h
            src/presenter/modelview/QualityCheckpointViewModel.h
            src/presenter/modelview/ControlPanelViewModel.h
            src/presenter/modelview/WorkUnitViewModel.h
            src/presenter/modelview/PlaceholderViewModels.h
)
target_link_libraries(objectsPresenter
    PUBLIC
        objectsModel
        Boost::signals2
        Threads::Threads
)

# View Layer - GTK4 UI
add_library(objectsView OBJECT)
target_sources(objectsView
    PRIVATE
        src/gtk/view/MainWindow.cpp
        src/gtk/view/DialogManager.cpp
        src/gtk/view/pages/DashboardPage.cpp
        src/gtk/view/pages/ProductsPage.cpp
    PRIVATE
        FILE_SET internalHeaders
        TYPE HEADERS
        FILES
            src/gtk/view/MainWindow.h
            src/gtk/view/DialogManager.h
            src/gtk/view/pages/DashboardPage.h
            src/gtk/view/pages/ProductsPage.h
)
target_link_libraries(objectsView
    PUBLIC
        objectsPresenter
        ${GTKMM_LIBRARIES}
        ${SQLITE_LIBRARIES}
)
target_include_directories(objectsView
    PUBLIC
        ${GTKMM_INCLUDE_DIRS}
        ${SQLITE_INCLUDE_DIRS}
)
target_compile_options(objectsView
    PUBLIC
        ${GTKMM_CFLAGS_OTHER}
)

# ============================================================================
# Main Executable (Cross-Platform)
# ============================================================================

# On Windows, create a GUI application (not console)
if(WIN32)
    add_executable(industrial-hmi WIN32)  # WIN32 = GUI app on Windows
else()
    add_executable(industrial-hmi)
endif()

target_sources(industrial-hmi
    PRIVATE
        src/main.cpp
)

# Link all object libraries
target_link_libraries(industrial-hmi
    PRIVATE
        objectsModel
        objectsConfig
        objectsPresenter
        objectsView
        Threads::Threads
)

# Windows: Add resource file if exists (icon, version info)
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/resources/app.rc")
    target_sources(industrial-hmi PRIVATE resources/app.rc)
endif()

# ============================================================================
# Copy Assets to Build Directory (Cross-Platform)
# ============================================================================

# Use platform-agnostic path separators
add_custom_target(CopyAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/assets
    COMMENT "📂 Copying assets to build directory"
)

add_custom_target(CopyConfig ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config
        ${CMAKE_BINARY_DIR}/config
    COMMENT "📂 Copying config files to build directory"
)

add_custom_target(CopyUIFiles ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/ui
        ${CMAKE_BINARY_DIR}/ui
    COMMENT "📂 Copying UI files to build directory"
)

add_dependencies(industrial-hmi CopyAssets CopyConfig CopyUIFiles)

# ============================================================================
# Installation Rules (Cross-Platform)
# ============================================================================

if(WIN32)
    # Windows: Install to Program Files
    set(CMAKE_INSTALL_PREFIX "C:/Program Files/IndustrialHMI" CACHE PATH "Install path" FORCE)
    install(TARGETS industrial-hmi RUNTIME DESTINATION .)
    install(DIRECTORY assets DESTINATION .)
    install(DIRECTORY ui DESTINATION .)
    
    # Install GTK4 runtime DLLs (if using vcpkg)
    # vcpkg automatically handles this with VCPKG_APPLOCAL_DEPS
else()
    # Linux/Unix: Standard FHS layout
    install(TARGETS industrial-hmi RUNTIME DESTINATION bin)
    install(DIRECTORY assets DESTINATION share/industrial-hmi)
    install(DIRECTORY ui DESTINATION share/industrial-hmi)
endif()

# ============================================================================
# CPack - Package Generation (Windows Installer / Linux Package)
# ============================================================================

set(CPACK_PACKAGE_NAME "IndustrialHMI")
set(CPACK_PACKAGE_VENDOR "Portfolio Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Industrial HMI - Cross-Platform MVP Application")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})

if(WIN32)
    # Windows: NSIS installer
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "Industrial HMI")
    set(CPACK_NSIS_PACKAGE_NAME "Industrial HMI ${PROJECT_VERSION}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
else()
    # Linux: DEB, RPM, TGZ
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Portfolio <portfolio@example.com>")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgtkmm-4.0-0, libsqlite3-0")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
endif()

include(CPack)

# ============================================================================
# Summary (Cross-Platform)
# ============================================================================

message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "  Industrial HMI - Cross-Platform Configuration")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Platform:       ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Architecture:   ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  C++ Standard:   C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build Type:     ${CMAKE_BUILD_TYPE}")
if(PLATFORM_LINUX OR PLATFORM_MACOS)
    message(STATUS "  GTK4:           ${GTKMM_VERSION}")
    message(STATUS "  SQLite:         ${SQLITE_VERSION}")
else()
    message(STATUS "  GTK4:           vcpkg")
    message(STATUS "  SQLite:         vcpkg")
endif()
message(STATUS "")
message(STATUS "Object Libraries:")
message(STATUS "  ✓ objectsModel      - Data layer (SimulatedModel, DatabaseManager)")
message(STATUS "  ✓ objectsPresenter  - Presenter layer (MVP orchestration)")
message(STATUS "  ✓ objectsView       - View layer (GTK4 pages)")
message(STATUS "")
if(WIN32)
    message(STATUS "🪟 Windows Build:")
    message(STATUS "  - Use vcpkg for dependencies")
    message(STATUS "  - GUI application (no console)")
    message(STATUS "  - CPack: NSIS installer + ZIP")
elseif(PLATFORM_LINUX)
    message(STATUS "🐧 Linux Build:")
    message(STATUS "  - pkg-config for dependencies")
    message(STATUS "  - CPack: DEB + RPM packages")
endif()
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "")
