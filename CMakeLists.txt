cmake_minimum_required(VERSION 3.20)

# Use BoostConfig.cmake instead of deprecated FindBoost module
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(industrial-hmi
    VERSION 1.0.0
    DESCRIPTION "Industrial HMI MVP Architecture - Cross-Platform (Linux/Windows)"
    LANGUAGES CXX)

# ============================================================================
# Platform Detection
# ============================================================================
if(WIN32)
    # Detect MSYS2/MinGW environment (use pkg-config like Linux)
    if(CMAKE_CXX_COMPILER MATCHES "msys64|clang64|mingw64|ucrt64")
        message(STATUS "Building for Windows (MSYS2/Clang64)")
        set(PLATFORM_LINUX TRUE)  # Use pkg-config path
        set(PLATFORM_MSYS2 TRUE)
    else()
        message(STATUS "Building for Windows")
        set(PLATFORM_WINDOWS TRUE)
    endif()
elseif(UNIX AND NOT APPLE)
    message(STATUS "Building for Linux")
    set(PLATFORM_LINUX TRUE)
elseif(APPLE)
    message(STATUS "Building for macOS")
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
        -Wno-unknown-warning-option   # Tolerate compiler-specific flags below
        -Wno-c2y-extensions           # Newer clang only; ignored elsewhere
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
# Unit Tests (opt-in)
# ============================================================================
option(BUILD_TESTS "Build unit tests (requires GoogleTest + gmock)" OFF)

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

# On Windows with vcpkg, set toolchain file
# MSYS2 users: dependencies via pacman (pkg-config)
if(PLATFORM_WINDOWS AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(WARNING "For Windows MSVC builds, use vcpkg toolchain file:")
    message(WARNING "    cmake -DCMAKE_TOOLCHAIN_FILE=[vcpkg]/scripts/buildsystems/vcpkg.cmake ..")
    message(WARNING "    OR use MSYS2 with: pacman -S mingw-w64-clang-x86_64-gtk4 gtkmm-4.0")
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

# MSYS2 Boost doesn't create component targets
# Use Boost::boost (headers) for header-only libraries
if(TARGET Boost::signals2)
    set(BOOST_TARGET Boost::signals2)
elseif(TARGET Boost::boost)
    set(BOOST_TARGET Boost::boost)
elseif(TARGET Boost::headers)
    set(BOOST_TARGET Boost::headers)
else()
    # Fallback: use include directories only
    set(BOOST_TARGET Boost::Boost)
endif()

# Threads (cross-platform)
find_package(Threads REQUIRED)

# ============================================================================
# Internationalization (gettext)
# ============================================================================
# Build catalog: po/<lang>.po -> <build>/locale/<lang>/LC_MESSAGES/<domain>.mo
set(GETTEXT_DOMAIN "industrial-hmi")
find_package(Gettext)  # msgfmt + msgmerge (translators only need the former)
find_package(Intl)     # libintl (MSVC/MinGW expose it as a separate library)

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
            src/model/Product.h
            src/model/ProductsRepository.h
            src/model/ProductionTypes.h
            src/model/ProductionModel.h
)
target_link_libraries(objectsModel
    PUBLIC
        ${GTKMM_LIBRARIES}
        ${SQLITE_LIBRARIES}
)
target_include_directories(objectsModel
    PUBLIC
        ${GTKMM_INCLUDE_DIRS}
        ${SQLITE_INCLUDE_DIRS}
)
target_compile_options(objectsModel
    PUBLIC
        ${GTKMM_CFLAGS_OTHER}
)

# Config Layer - JSON configuration management
add_library(objectsConfig INTERFACE)
target_sources(objectsConfig
    INTERFACE
        FILE_SET HEADERS
        FILES
            src/config/ConfigManager.h
            src/config/config_defaults.h
)

# Core Layer - GTK-free utilities: Logger, i18n mechanism, Result, CsvSerializer
# Bootstrap orchestrates staged startup (logger -> config -> i18n) and is
# reused by every entry point (GTK today, headless console next).
add_library(objectsCore OBJECT)
target_sources(objectsCore
    PRIVATE
        src/core/i18n.cpp
        src/core/CsvSerializer.cpp
        src/core/Bootstrap.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/core/LoggerBase.h
            src/core/LoggerImpl.h
            src/core/ExceptionHandler.h
            src/core/Result.h
            src/core/ErrorHandling.h
            src/core/i18n.h
            src/core/CsvSerializer.h
            src/core/Bootstrap.h
)
target_link_libraries(objectsCore
    PUBLIC
        objectsModel
        objectsConfig
        # NOTE: deliberately no gtkmm — this library is GTK-free by
        # construction so the headless console binary can link it
        # without dragging in a GUI toolkit.
)
target_compile_definitions(objectsCore
    PUBLIC
        GETTEXT_PACKAGE="${GETTEXT_DOMAIN}"
)

# libintl on platforms where it isn't merged into libc (Windows, macOS)
if(Intl_FOUND)
    target_include_directories(objectsCore PUBLIC ${Intl_INCLUDE_DIRS})
    target_link_libraries(objectsCore PUBLIC ${Intl_LIBRARIES})
endif()

# AppGtk Layer - GTK-specific application bootstrap. Kept separate from
# objectsCore so console binaries can link objectsCore without inheriting
# gtkmm as a transitive dependency.
add_library(objectsAppGtk OBJECT)
target_sources(objectsAppGtk
    PRIVATE
        src/core/Application.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/core/Application.h
)
target_link_libraries(objectsAppGtk
    PUBLIC
        objectsCore
        objectsModel
        ${GTKMM_LIBRARIES}
)
target_include_directories(objectsAppGtk
    PUBLIC
        ${GTKMM_INCLUDE_DIRS}
)
target_compile_options(objectsAppGtk
    PUBLIC
        ${GTKMM_CFLAGS_OTHER}
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
        ${BOOST_TARGET}
        Threads::Threads
)

# View Layer - GTK4 UI
add_library(objectsView OBJECT)
target_sources(objectsView
    PRIVATE
        src/gtk/view/MainWindow.cpp
        src/gtk/view/DialogManager.cpp
        src/gtk/view/AboutDialog.cpp
        src/gtk/view/pages/DashboardPage.cpp
        src/gtk/view/pages/ProductsPage.cpp
        src/gtk/view/pages/SettingsPage.cpp
    PRIVATE
        FILE_SET internalHeaders
        TYPE HEADERS
        FILES
            src/gtk/view/MainWindow.h
            src/gtk/view/DialogManager.h
            src/gtk/view/AboutDialog.h
            src/gtk/view/pages/Page.h
            src/gtk/view/pages/DashboardPage.h
            src/gtk/view/pages/ProductsPage.h
            src/gtk/view/pages/SettingsPage.h
            src/gtk/view/pages/ProductObject.h
            src/gtk/view/widgets/QualityGauge.h
            src/gtk/view/widgets/TrendChart.h
            src/gtk/view/widgets/AlertsPanel.h
            src/gtk/view/widgets/SystemStatusBadge.h
            src/gtk/view/widgets/LiveClock.h
            src/presenter/AlertCenter.h
            src/presenter/modelview/AlertViewModel.h
            src/gtk/view/css_classes.h
            src/gtk/view/ui_sizes.h
            src/gtk/view/colors.h
)
target_link_libraries(objectsView
    PUBLIC
        objectsPresenter
        objectsAppGtk
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
        objectsCore
        objectsAppGtk
        objectsPresenter
        objectsView
        Threads::Threads
)

# Windows: Boost.Asio requires Winsock
if(WIN32)
    target_link_libraries(industrial-hmi PRIVATE ws2_32)
endif()

# libintl - needed on platforms where gettext is not in libc (Windows, macOS)
if(Intl_FOUND)
    target_include_directories(industrial-hmi PRIVATE ${Intl_INCLUDE_DIRS})
    target_link_libraries(industrial-hmi PRIVATE ${Intl_LIBRARIES})
endif()

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
    COMMENT "Copying assets to build directory"
)

add_custom_target(CopyConfig ALL
    # Nuke the runtime copy first so user-modified fields (e.g. the
    # last language chosen in Settings) don't survive a rebuild —
    # every build starts clean from the pristine source config.
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config
        ${CMAKE_BINARY_DIR}/config
    COMMENT "Copying config files to build directory"
)

# CopyUIFiles removed — UI layouts and CSS now live under assets/ui and
# assets/styles, so CopyAssets already covers them.

add_dependencies(industrial-hmi CopyAssets CopyConfig)

# ============================================================================
# Translation Catalogs (.po -> .mo)
# ============================================================================
set(PO_DIR "${CMAKE_SOURCE_DIR}/po")
set(LOCALE_BUILD_DIR "${CMAKE_BINARY_DIR}/locale")

# Tell main.cpp where runtime catalogs live (dev: build tree, install: share)
target_compile_definitions(industrial-hmi PRIVATE
    LOCALE_DIR="${LOCALE_BUILD_DIR}"
)

if(GETTEXT_MSGFMT_EXECUTABLE AND EXISTS "${PO_DIR}/LINGUAS")
    file(STRINGS "${PO_DIR}/LINGUAS" LINGUAS_RAW)
    set(MO_FILES "")
    foreach(line IN LISTS LINGUAS_RAW)
        string(STRIP "${line}" lang)
        if(lang AND NOT lang MATCHES "^#")
            set(po_file "${PO_DIR}/${lang}.po")
            set(mo_dir  "${LOCALE_BUILD_DIR}/${lang}/LC_MESSAGES")
            set(mo_file "${mo_dir}/${GETTEXT_DOMAIN}.mo")
            add_custom_command(
                OUTPUT "${mo_file}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${mo_dir}"
                COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} -o "${mo_file}" "${po_file}"
                DEPENDS "${po_file}"
                COMMENT "Compiling ${lang}.po -> ${GETTEXT_DOMAIN}.mo"
                VERBATIM
            )
            list(APPEND MO_FILES "${mo_file}")

            # Install compiled catalog
            if(WIN32)
                install(FILES "${mo_file}"
                        DESTINATION "locale/${lang}/LC_MESSAGES")
            else()
                install(FILES "${mo_file}"
                        DESTINATION "share/locale/${lang}/LC_MESSAGES")
            endif()
        endif()
    endforeach()
    if(MO_FILES)
        add_custom_target(translations ALL DEPENDS ${MO_FILES})
        add_dependencies(industrial-hmi translations)
    endif()
else()
    message(STATUS "gettext msgfmt not found - translations will not be compiled")
endif()

# ============================================================================
# Installation Rules (Cross-Platform)
# ============================================================================

if(WIN32)
    # Windows: Install to Program Files
    set(CMAKE_INSTALL_PREFIX "C:/Program Files/IndustrialHMI" CACHE PATH "Install path" FORCE)
    install(TARGETS industrial-hmi RUNTIME DESTINATION .)
    install(DIRECTORY assets DESTINATION .)

    # Install GTK4 runtime DLLs (if using vcpkg)
    # vcpkg automatically handles this with VCPKG_APPLOCAL_DEPS
else()
    # Linux/Unix: Standard FHS layout
    install(TARGETS industrial-hmi RUNTIME DESTINATION bin)
    install(DIRECTORY assets DESTINATION share/industrial-hmi)
endif()

# ============================================================================
# Tests (only when BUILD_TESTS=ON)
# ============================================================================
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
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
message(STATUS "  objectsConfig     - INTERFACE: JSON config defaults + ConfigManager (policy)")
message(STATUS "  objectsModel      - Data layer (SimulatedModel, DatabaseManager)")
message(STATUS "  objectsCore       - Logger, i18n mechanism, Bootstrap (GTK-free)")
message(STATUS "  objectsAppGtk     - GTK application bootstrap (Application.cpp)")
message(STATUS "  objectsPresenter  - Presenter layer (MVP orchestration)")
message(STATUS "  objectsView       - View layer (GTK4 pages)")
message(STATUS "")
message(STATUS "Dependency graph (low -> high):")
message(STATUS "  Config -> Model -> Core -> {AppGtk, Presenter}")
message(STATUS "                                     |")
message(STATUS "                                    View -> industrial-hmi (GTK exe)")
message(STATUS "")
if(WIN32)
    message(STATUS "Windows Build:")
    message(STATUS "  - Use vcpkg for dependencies")
    message(STATUS "  - GUI application (no console)")
    message(STATUS "  - CPack: NSIS installer + ZIP")
elseif(PLATFORM_LINUX)
    message(STATUS "Linux Build:")
    message(STATUS "  - pkg-config for dependencies")
    message(STATUS "  - CPack: DEB + RPM packages")
endif()
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "")
