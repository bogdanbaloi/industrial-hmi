cmake_minimum_required(VERSION 3.20)

# Use BoostConfig.cmake instead of deprecated FindBoost module
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(industrial-hmi
    VERSION 1.2.0
    DESCRIPTION "Industrial HMI MVP Architecture - Cross-Platform (Linux/Windows)"
    LANGUAGES CXX)

# Custom Find* modules live in cmake/. Currently FindOnnxRuntime.cmake
# only -- Microsoft does not ship a CMake config in their prebuilt
# tarball, so we hand-rolled one.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Platform Detection
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

# C++20 Standard (Cross-Platform)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warnings (Platform-Specific)
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

# Sanitizers (Debug builds - GCC/Clang only)
# ASan + UBSan and TSan are mutually exclusive at the linker level (libasan
# and libtsan both intercept the same C runtime hooks), so each lives behind
# its own flag and is exercised by a separate CI job.
option(ENABLE_SANITIZERS "Enable sanitizers (AddressSanitizer + UBSanitizer)" OFF)
option(ENABLE_TSAN       "Enable ThreadSanitizer (mutually exclusive with ENABLE_SANITIZERS)" OFF)

if(ENABLE_SANITIZERS AND ENABLE_TSAN)
    message(FATAL_ERROR
        "ENABLE_SANITIZERS and ENABLE_TSAN cannot be combined: "
        "libasan and libtsan conflict at link time. Pick one.")
endif()

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
endif()

if(ENABLE_TSAN AND NOT MSVC)
    message(STATUS "ThreadSanitizer enabled")

    # TSan requires PIE on most distros; -fno-omit-frame-pointer keeps the
    # stack traces meaningful (TSan's race reports walk both the racing
    # thread's stack and the previously-accessing thread's stack).
    set(TSAN_FLAGS
        -fsanitize=thread
        -fno-omit-frame-pointer
        -g
    )

    add_compile_options(${TSAN_FLAGS})
    add_link_options(${TSAN_FLAGS})
endif()

# Code Coverage (Debug builds - GCC/Clang only)
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

# Unit Tests (opt-in)
option(BUILD_TESTS "Build unit tests (requires GoogleTest + gmock)" OFF)

# ML inference -- requires ONNX Runtime. Default OFF so the existing
# build path (no Edge AI dependency) keeps working unchanged. CI turns
# this ON for jobs that have ONNX Runtime provisioned.
option(BUILD_ML_CLASSIFIER
    "Compile OnnxImageClassifier (requires ONNX Runtime)" OFF)

# Platform-Specific Definitions
if(WIN32)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS     # Disable MSVC secure warnings
        NOMINMAX                     # Don't define min/max macros
        WIN32_LEAN_AND_MEAN         # Exclude rarely-used Windows headers
    )
endif()

# Find Dependencies (Cross-Platform)

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
    # libsodium for Argon2id password hashing in the auth layer.
    # OWASP-recommended KDF as of 2024; resists GPU brute-force +
    # side-channel attacks. Ubuntu/MSYS2 ship .pc files; Windows native
    # picks up the vcpkg target below.
    pkg_check_modules(SODIUM REQUIRED libsodium)
else()
    # Windows: Use vcpkg packages
    find_package(gtkmm REQUIRED)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
    find_package(unofficial-sodium CONFIG REQUIRED)
    set(GTKMM_LIBRARIES gtkmm::gtkmm)
    set(SQLITE_LIBRARIES unofficial::sqlite3::sqlite3)
    set(SODIUM_LIBRARIES unofficial-sodium::sodium)
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

# Internationalization (gettext)
# Build catalog: po/<lang>.po -> <build>/locale/<lang>/LC_MESSAGES/<domain>.mo
set(GETTEXT_DOMAIN "industrial-hmi")
find_package(Gettext)  # msgfmt + msgmerge (translators only need the former)
find_package(Intl)     # libintl (MSVC/MinGW expose it as a separate library)

# stb_image - public-domain single-header image decoder (PNG / JPEG / BMP /
# ...). Used by src/ml/ImageDecoder.cpp; nothing else in the project sees
# it. Pinned to a specific upstream commit for reproducibility; FetchContent
# caches under build/_deps/ across builds so the network hit happens once.
#
# SOURCE_DIR is overridden away from the default `_deps/stb_image-src/`
# because the CI clang-tidy step uses `-header-filter='src/.*'`, which
# substring-matches paths containing "src/" anywhere -- including
# "_deps/stb_image-src/". The resulting analyzer diagnostics inside the
# vendored header would fail the build. Routing into `_deps/stb-vendor/`
# keeps the regex unmatched so third-party code stays out of our checks.
include(FetchContent)
FetchContent_Declare(stb_image
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        f0569113c93ad095470c54bf34a17b36646bbbb5
    GIT_SHALLOW    FALSE
    SOURCE_DIR     ${CMAKE_BINARY_DIR}/_deps/stb-vendor
)
FetchContent_MakeAvailable(stb_image)

# open62541 -- OPC-UA stack (MPL 2.0). Industrial protocol used by the
# OPC-UA server backend (src/integration/opcua/). Default OFF so the
# baseline build stays small; CI turns this ON for the integration job.
#
# Pinned to v1.5.4 (current stable as of writing). Built as a STATIC
# library to avoid DLL-deployment headaches on Windows -- adds ~2.4MB
# to the host binary, which is acceptable given the alternative is
# shipping libopen62541.dll alongside every release artefact.
#
# UA_NAMESPACE_ZERO=REDUCED ships only the OPC-UA Foundation node
# definitions we actually use (Server, Variable, Method, BaseObject).
# FULL adds historical access, alarms+events, programs, etc. -- all
# out of scope for this project's reference implementation.
#
# SOURCE_DIR routing (same rationale as stb_image above): clang-tidy
# uses -header-filter='src/.*' which substring-matches "src/" anywhere
# including "_deps/open62541-src/". Routing to "_deps/open62541-vendor/"
# keeps the vendored C source out of our static-analysis surface.
option(BUILD_OPCUA_BACKEND
    "Compile OPC-UA backend (downloads + builds open62541)" OFF)

# Modbus master backend: hand-rolled MBAP framing over Boost.Asio TCP.
# No third-party Modbus library required, so this is on by default --
# any platform with Boost.Asio can build it.
option(BUILD_MODBUS_BACKEND
    "Compile Modbus master backend (hand-rolled MBAP over Boost.Asio)" ON)

if(BUILD_OPCUA_BACKEND)
    message(STATUS "OPC-UA backend enabled -- fetching open62541 v1.5.4")

    # Suppress diagnostics from the vendored C code; we treat warnings
    # as errors in our own targets but open62541 is third-party and
    # ships with its own clean build at upstream.
    set(UA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(UA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(UA_NAMESPACE_ZERO REDUCED CACHE STRING "" FORCE)
    set(BUILD_SHARED_LIBS_save ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(open62541
        GIT_REPOSITORY https://github.com/open62541/open62541.git
        GIT_TAG        v1.5.4
        GIT_SHALLOW    TRUE
        SOURCE_DIR     ${CMAKE_BINARY_DIR}/_deps/open62541-vendor
    )
    FetchContent_MakeAvailable(open62541)

    # Restore the project-wide BUILD_SHARED_LIBS preference so any
    # downstream FetchContent declarations use the original value.
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_save} CACHE BOOL "" FORCE)

    # open62541's generated namespace0_generated.c emits string literals
    # well past C99's 4095-char minimum-supported length. Upstream's
    # own build adds -Wno-overlength-strings, but our project-wide
    # -Wpedantic + -Werror (added AFTER their per-target flags) re-
    # enables the warning and escalates it to error on Clang 21
    # (MSYS2 Clang64). Demote it back to a non-error on each open62541
    # target so the vendored C source compiles cleanly without
    # touching upstream code.
    foreach(_o62541_target IN ITEMS open62541-object open62541-plugins)
        if(TARGET ${_o62541_target})
            target_compile_options(${_o62541_target} PRIVATE
                -Wno-error=overlength-strings
                -Wno-error=pedantic)
        endif()
    endforeach()
endif()

# Object Libraries (Modular Build Pattern)

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
# StartupErrors + StartupDialog form the fatal-reporting pipeline: typed
# exceptions thrown from Bootstrap / Application land in main()'s guard
# and surface through a native per-platform reporter.
add_library(objectsCore OBJECT)
target_sources(objectsCore
    PRIVATE
        src/core/i18n.cpp
        src/core/Bootstrap.cpp
        src/core/StartupDialog.cpp
        src/core/TimeFormat.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/core/LoggerBase.h
            src/core/LoggerImpl.h
            src/core/ExceptionHandler.h
            src/core/Result.h
            src/core/ErrorHandling.h
            src/core/i18n.h
            src/core/Bootstrap.h
            src/core/StartupErrors.h
            src/core/StartupDialog.h
            src/core/TimeFormat.h
)
target_link_libraries(objectsCore
    PUBLIC
        objectsModel
        objectsConfig
        # NOTE: deliberately no gtkmm -- this library is GTK-free by
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
        src/gtk/view/LoginDialog.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/core/Application.h
            src/gtk/view/LoginDialog.h
)
target_link_libraries(objectsAppGtk
    PUBLIC
        objectsCore
        objectsModel
        objectsAuth
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
        src/presenter/QualityInspectionPresenter.cpp
        src/presenter/BackendHealthPresenter.cpp
        src/presenter/UsersPresenter.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/presenter/BasePresenter.h
            src/presenter/DashboardPresenter.h
            src/presenter/ProductsPresenter.h
            src/presenter/QualityInspectionPresenter.h
            src/presenter/BackendHealthPresenter.h
            src/presenter/UsersPresenter.h
            src/presenter/ViewObserver.h
            src/presenter/modelview/BackendHealthViewModel.h
            src/presenter/modelview/EquipmentCardViewModel.h
            src/presenter/modelview/QualityCheckpointViewModel.h
            src/presenter/modelview/ControlPanelViewModel.h
            src/presenter/modelview/WorkUnitViewModel.h
            src/presenter/modelview/InspectionResultViewModel.h
            src/presenter/modelview/PlaceholderViewModels.h
)
# QualityInspectionPresenter depends on the ML interfaces; the Backend
# Health presenter depends on objectsIntegrationCore (IntegrationManager
# + IntegrationBackend). Both are GTK-free.
target_link_libraries(objectsPresenter
    PUBLIC
        objectsModel
        objectsMl
        objectsIntegrationCore
        objectsAuth
        ${BOOST_TARGET}
        Threads::Threads
)

# ----------------------------------------------------------------
# Integration Layer -- split per protocol.
#
# Pattern: every wire protocol lives in its own object library so a
# build can include only the protocols it actually ships, and so
# operators can map "the Modbus pill in the I/O panel" to a
# distinct compile unit on the link line.
#
#   objectsIntegrationCore   Interfaces + manager + serializers +
#                            telemetry bridges. Protocol-agnostic.
#                            Bridges depend on TelemetryPublisher /
#                            TelemetrySubscriber abstractions, so
#                            they live here -- adding a new protocol
#                            does not bring new bridge code.
#   objectsTcp               TcpBackend.{h,cpp}.
#   objectsMqtt              MqttPacket + MqttClient.
#   objectsOpcUa             (declared further down, gated on
#                            BUILD_OPCUA_BACKEND).
#
# A library / executable that only needs the manager + interfaces
# links objectsIntegrationCore alone. Only main.cpp's executable
# targets link the protocol-specific libs.
# ----------------------------------------------------------------

# ----- objectsIntegrationCore -----
add_library(objectsIntegrationCore OBJECT)
target_sources(objectsIntegrationCore
    PRIVATE
        src/integration/CsvSerializer.cpp
        src/integration/JsonSerializer.cpp
        src/integration/IntegrationManager.cpp
        src/integration/ProductionTelemetryBridge.cpp
        src/integration/PlainTextTelemetryFormatter.cpp
        src/integration/JsonTelemetryFormatter.cpp
        src/integration/SensorIngestBridge.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/integration/Serializer.h
            src/integration/CsvSerializer.h
            src/integration/JsonSerializer.h
            src/integration/IntegrationBackend.h
            src/integration/IntegrationManager.h
            src/integration/TelemetryPublisher.h
            src/integration/TelemetrySubscriber.h
            src/integration/TelemetryPayloadFormatter.h
            src/integration/PlainTextTelemetryFormatter.h
            src/integration/JsonTelemetryFormatter.h
            src/integration/ProductionTelemetryBridge.h
            src/integration/SensorIngestBridge.h
)
target_link_libraries(objectsIntegrationCore
    PUBLIC
        objectsModel
        Threads::Threads
)
# NOTE: core has NO Boost dep -- only the protocol libs that
# actually open sockets do.

# ----- objectsTcp -----
# TCP line-protocol backend. Boost.Asio acceptor + std::jthread per
# session. Implements IntegrationBackend from the core lib.
add_library(objectsTcp OBJECT)
target_sources(objectsTcp
    PRIVATE
        src/integration/TcpBackend.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/integration/TcpBackend.h
)
target_link_libraries(objectsTcp
    PUBLIC
        objectsIntegrationCore
        objectsModel
        ${BOOST_TARGET}
        Threads::Threads
)
if(WIN32)
    # Boost.Asio acceptor uses IOCP AcceptEx / GetAcceptExSockaddrs
    # (in mswsock.lib) on top of ws2_32 Winsock.
    target_link_libraries(objectsTcp PUBLIC ws2_32 mswsock)
endif()

# ----- objectsMqtt -----
# MQTT 3.1.1 hand-rolled client (no paho dep). Implements both
# TelemetryPublisher and TelemetrySubscriber so the same socket
# carries outbound publishes and inbound subscriptions.
add_library(objectsMqtt OBJECT)
target_sources(objectsMqtt
    PRIVATE
        src/integration/MqttPacket.cpp
        src/integration/MqttClient.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/integration/MqttPacket.h
            src/integration/MqttClient.h
)
target_link_libraries(objectsMqtt
    PUBLIC
        objectsIntegrationCore
        ${BOOST_TARGET}
        Threads::Threads
)
if(WIN32)
    target_link_libraries(objectsMqtt PUBLIC ws2_32 mswsock)
endif()

# OPC-UA Layer - Industrial protocol backend. Lives in its own object
# library so the open62541 dependency is contained: only consumers that
# need OPC-UA pull in libopen62541.a (~5MB static). The interfaces in
# src/integration/opcua/ stay open62541-free so unit tests can mock
# the server abstraction without dragging in the C stack.
#
# Compiled only when BUILD_OPCUA_BACKEND=ON; the host binary still
# builds and runs without OPC-UA when this is off (the IntegrationManager
# simply doesn't register an OpcUaBackend).
if(BUILD_OPCUA_BACKEND)
    add_library(objectsOpcUa OBJECT)
    target_sources(objectsOpcUa
        PRIVATE
            src/integration/opcua/Open62541Server.cpp
            src/integration/opcua/FactoryNodeMap.cpp
            src/integration/opcua/OpcUaBackend.cpp
            src/integration/opcua/Open62541Client.cpp
            src/integration/opcua/OpcUaIngestBridge.cpp
            src/integration/opcua/FactoryCommandSink.cpp
        PUBLIC
            FILE_SET HEADERS
            FILES
                src/integration/opcua/OpcUaConfig.h
                src/integration/opcua/OpcUaServer.h
                src/integration/opcua/OpcUaCommandSink.h
                src/integration/opcua/OpcUaNodeMap.h
                src/integration/opcua/Open62541Server.h
                src/integration/opcua/FactoryNodeMap.h
                src/integration/opcua/OpcUaBackend.h
                src/integration/opcua/OpcUaClient.h
                src/integration/opcua/Open62541Client.h
                src/integration/opcua/OpcUaIngestBridge.h
                src/integration/opcua/FactoryCommandSink.h
    )
    target_link_libraries(objectsOpcUa
        PUBLIC
            objectsCore         # Logger, used by Open62541Server
            objectsModel        # ProductionModel, used by FactoryNodeMap
            objectsIntegrationCore  # IntegrationBackend, base of OpcUaBackend
            open62541::open62541
            Threads::Threads
    )
    target_include_directories(objectsOpcUa
        PUBLIC
            ${CMAKE_SOURCE_DIR}
    )
    target_compile_definitions(objectsOpcUa
        PUBLIC
            INDUSTRIAL_HMI_HAS_OPCUA_BACKEND=1
    )
endif()

# Modbus Layer - Industrial polling protocol backend. Lives in its own
# object library so the wire codec stays isolated from MQTT / OPC-UA
# concerns and CI can compile/test it without the OPC-UA toolchain.
# Pure hand-rolled MBAP framing -- no libmodbus dependency.
#
# Compiled only when BUILD_MODBUS_BACKEND=ON (default ON) so the host
# binary builds without Modbus on platforms where Boost.Asio is
# unavailable; the IntegrationManager simply doesn't register a
# ModbusBackend in that case.
if(BUILD_MODBUS_BACKEND)
    add_library(objectsModbus OBJECT)
    target_sources(objectsModbus
        PRIVATE
            src/integration/modbus/ModbusPdu.cpp
            src/integration/modbus/ModbusClient.cpp
            src/integration/modbus/ModbusIngestBridge.cpp
            src/integration/modbus/ModbusPollLoop.cpp
            src/integration/modbus/ModbusBackend.cpp
        PUBLIC
            FILE_SET HEADERS
            FILES
                src/integration/modbus/ModbusPdu.h
                src/integration/modbus/ModbusReader.h
                src/integration/modbus/ModbusClient.h
                src/integration/modbus/ModbusRegisterMap.h
                src/integration/modbus/ModbusIngestBridge.h
                src/integration/modbus/ModbusPollLoop.h
                src/integration/modbus/ModbusBackend.h
    )
    target_link_libraries(objectsModbus
        PUBLIC
            objectsCore             # Result<T,E>, Logger
            objectsModel            # ProductionModel
            objectsIntegrationCore  # IntegrationBackend, base for ModbusBackend
            ${BOOST_TARGET}         # Boost.Asio (ModbusClient transport)
            Threads::Threads
    )
    target_include_directories(objectsModbus
        PUBLIC
            ${CMAKE_SOURCE_DIR}
    )
    target_compile_definitions(objectsModbus
        PUBLIC
            INDUSTRIAL_HMI_HAS_MODBUS_BACKEND=1
    )
endif()

# ML Layer - Edge inference foundation. Image decoding (stb_image),
# preprocessing (resize / centre-crop / ImageNet normalise), label
# table, ImageClassifier interface + FakeImageClassifier test double.
# The ONNX-runtime-backed classifier (OnnxImageClassifier) compiles
# only when BUILD_ML_CLASSIFIER=ON, so the foundation pieces stay
# usable on builds without an ONNX Runtime install.
#
# STATIC, not OBJECT: objectsPresenter depends on this for
# QualityInspectionPresenter, and CMake OBJECT libraries do not
# propagate their object files transitively through other OBJECT
# libraries to downstream test binaries. STATIC archives propagate
# correctly, and the linker dead-strips symbols not referenced by
# the final binary so the size impact is nil.
add_library(objectsMl STATIC)
target_sources(objectsMl
    PRIVATE
        src/ml/ImageDecoder.cpp
        src/ml/ImageNetPreprocessor.cpp
        src/ml/ImageNetLabels.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/ml/Image.h
            src/ml/ImageDecoder.h
            src/ml/Preprocessor.h
            src/ml/ImageNetPreprocessor.h
            src/ml/ImageNetLabels.h
            src/ml/Classification.h
            src/ml/ImageClassifier.h
            src/ml/FakeImageClassifier.h
)
# stb_image.h sits in the FetchContent source dir; PRIVATE because no
# header in our public surface re-exposes it. SYSTEM marks the include
# as third-party so clang-tidy skips diagnostics inside the header --
# stb is C-style code that intentionally trips every modern C++ rule
# (magic numbers, signed bitwise, manual malloc, identifier naming).
target_include_directories(objectsMl SYSTEM PRIVATE ${stb_image_SOURCE_DIR})

# Optional: compile OnnxImageClassifier facade + the runtime plugin
# when BUILD_ML_CLASSIFIER=ON.
#
# Architecture: the host binary (`industrial-hmi.exe`) NEVER links
# libonnxruntime directly. Instead, ORT-touching code lives in a
# separate SHARED MODULE library (`industrial_ml_ort.{dll,so,dylib}`)
# loaded at runtime via dlopen / LoadLibrary on the first
# `OnnxImageClassifier` construction. This keeps the GTK4 process
# boot path clean of the symbol-resolution conflicts that otherwise
# crash gtkmm widget registration when libonnxruntime is in the
# address space.
#
# objectsMl contains only the dlopen facade + foundation pieces and
# does NOT link libonnxruntime. The plugin module compiles
# onnx_plugin.cpp + a private rebuild of the foundation sources it
# needs (Image, Preprocessor, Labels) so it is a self-contained unit
# the loader can drop in next to the binary.
if(BUILD_ML_CLASSIFIER)
    find_package(OnnxRuntime REQUIRED)
    message(STATUS "ONNX Runtime: ${OnnxRuntime_VERSION} "
                   "(${OnnxRuntime_LIBRARY})")

    # Facade compiled INTO objectsMl. Pure dlopen wrapper; no ORT link.
    target_sources(objectsMl
        PRIVATE
            src/ml/OnnxImageClassifier.cpp
        PUBLIC
            FILE_SET HEADERS
            FILES
                src/ml/OnnxImageClassifier.h
    )
    if(NOT WIN32)
        target_link_libraries(objectsMl PUBLIC ${CMAKE_DL_LIBS})
    endif()
    target_compile_definitions(objectsMl PUBLIC INDUSTRIAL_HMI_HAS_ML_PLUGIN=1)

    # Plugin module. SHARED MODULE so it is dlopen-only (cannot be
    # link-time consumed by other targets). Output name forced to
    # "industrial_ml_ort" without per-platform "lib" prefix on Windows
    # so dlopen finds it under a uniform filename.
    add_library(industrial_ml_ort MODULE
        src/ml/onnx_plugin.cpp
        src/ml/ImageNetPreprocessor.cpp
        src/ml/ImageNetLabels.cpp
    )
    target_include_directories(industrial_ml_ort
        PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(industrial_ml_ort
        PRIVATE OnnxRuntime::OnnxRuntime)
    set_target_properties(industrial_ml_ort PROPERTIES
        OUTPUT_NAME industrial_ml_ort
        POSITION_INDEPENDENT_CODE ON
        # Drop the plugin next to the host binary so dlopen / LoadLibrary
        # finds it via the platform default loader path (current
        # working directory on Windows, $ORIGIN-style on Linux when
        # LD_LIBRARY_PATH covers it).
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    # Hide all non-exported symbols so the plugin's private copy of
    # the foundation sources cannot accidentally leak into another
    # consumer that dlopens with RTLD_GLOBAL.
    if(NOT MSVC)
        target_compile_options(industrial_ml_ort PRIVATE -fvisibility=hidden)
    endif()
endif()

# Historian Layer - Time-series persistence for sensor data.
# GTK-free OBJECT lib so it can be linked into either front-end. The
# concrete SqliteHistoryStore implements both HistoryWriter and
# HistoryReader; the bridge (added in a follow-up commit) subscribes
# to ProductionModel signals and batches writes through the writer
# interface. Read side is consumed by the GTK HistoryPage and
# (eventually) a console `history` command.
add_library(objectsHistorian OBJECT)
target_sources(objectsHistorian
    PRIVATE
        src/historian/SqliteHistoryStore.cpp
        src/historian/HistorianBridge.cpp
        src/historian/HistorianMaintenance.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/historian/HistoryRecord.h
            src/historian/HistoryWriter.h
            src/historian/HistoryReader.h
            src/historian/SqliteHistoryStore.h
            src/historian/HistorianBridge.h
            src/historian/HistorianMaintenance.h
)
target_link_libraries(objectsHistorian
    PUBLIC
        objectsCore
        objectsModel
        ${SQLITE_LIBRARIES}
)
target_include_directories(objectsHistorian
    PUBLIC
        ${SQLITE_INCLUDE_DIRS}
)

# Auth Layer - Username/password authentication, RBAC, audit log.
# GTK-free OBJECT lib (the UI dialog lives in objectsView) so both
# front-ends and the test suite can link the auth core without
# pulling GTK. libsodium provides the Argon2id KDF.
add_library(objectsAuth OBJECT)
target_sources(objectsAuth
    PRIVATE
        src/auth/Argon2PasswordHasher.cpp
        src/auth/SqliteUserRepository.cpp
        src/auth/SqliteAuditLogger.cpp
        src/auth/AuthService.cpp
        src/auth/AvatarPlaceholder.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/auth/Role.h
            src/auth/User.h
            src/auth/PasswordHasher.h
            src/auth/Argon2PasswordHasher.h
            src/auth/UserRepository.h
            src/auth/SqliteUserRepository.h
            src/auth/AuditEvent.h
            src/auth/AuditLogger.h
            src/auth/SqliteAuditLogger.h
            src/auth/Session.h
            src/auth/AuthService.h
            src/auth/AvatarPlaceholder.h
)
target_link_libraries(objectsAuth
    PUBLIC
        objectsCore
        ${SQLITE_LIBRARIES}
        ${SODIUM_LIBRARIES}
)
target_include_directories(objectsAuth
    PUBLIC
        ${SQLITE_INCLUDE_DIRS}
        ${SODIUM_INCLUDE_DIRS}
)

# Console Layer - Headless front-end. Shares the Model + Presenter
# layer with the GTK View, proving MVP's View-swap promise. Links zero
# gtkmm (see architecture receipt in README).
add_library(objectsConsole OBJECT)
target_sources(objectsConsole
    PRIVATE
        src/console/ConsoleView.cpp
        src/console/InitConsole.cpp
    PUBLIC
        FILE_SET HEADERS
        FILES
            src/console/ConsoleView.h
            src/console/InitConsole.h
)
target_link_libraries(objectsConsole
    PUBLIC
        objectsPresenter
        objectsModel
        objectsCore
        ${BOOST_TARGET}
        Threads::Threads
)

# View Layer - GTK4 UI
add_library(objectsView OBJECT)
target_sources(objectsView
    PRIVATE
        src/gtk/view/MainWindow.cpp
        src/gtk/view/MainWindowKeyDispatch.cpp
        src/gtk/view/DialogManager.cpp
        src/gtk/view/AboutDialog.cpp
        src/gtk/view/pages/AuditLogPage.cpp
        src/gtk/view/pages/DashboardPage.cpp
        src/gtk/view/pages/HistoryPage.cpp
        src/gtk/view/pages/ProductsPage.cpp
        src/gtk/view/pages/SettingsPage.cpp
        src/gtk/view/pages/QualityInspectionPage.cpp
        src/gtk/view/pages/UsersPage.cpp
        src/gtk/view/dialogs/UserEditDialog.cpp
        src/gtk/view/dialogs/ResetPasswordDialog.cpp
        src/gtk/view/dialogs/ProfileDialog.cpp
        src/gtk/view/widgets/UserBadge.cpp
        src/gtk/view/widgets/AvatarWidget.cpp
        src/gtk/view/widgets/Toast.cpp
    PRIVATE
        FILE_SET internalHeaders
        TYPE HEADERS
        FILES
            src/gtk/view/MainWindow.h
            src/gtk/view/MainWindowKeyDispatch.h
            src/gtk/view/DialogManager.h
            src/gtk/view/AboutDialog.h
            src/gtk/view/pages/Page.h
            src/gtk/view/pages/AuditLogPage.h
            src/gtk/view/pages/DashboardPage.h
            src/gtk/view/pages/HistoryPage.h
            src/gtk/view/pages/ProductsPage.h
            src/gtk/view/pages/SettingsPage.h
            src/gtk/view/pages/QualityInspectionPage.h
            src/gtk/view/pages/UsersPage.h
            src/gtk/view/pages/ProductObject.h
            src/gtk/view/dialogs/UserEditDialog.h
            src/gtk/view/dialogs/ResetPasswordDialog.h
            src/gtk/view/dialogs/ProfileDialog.h
            src/gtk/view/widgets/AvatarWidget.h
            src/gtk/view/widgets/QualityGauge.h
            src/gtk/view/widgets/TrendChart.h
            src/gtk/view/widgets/AlertsPanel.h
            src/gtk/view/widgets/SystemStatusBadge.h
            src/gtk/view/widgets/LiveClock.h
            src/gtk/view/widgets/UserBadge.h
            src/gtk/view/widgets/Toast.h
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
        objectsIntegrationCore
        objectsHistorian
        objectsAuth
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

# Main Executable (Cross-Platform)

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

# Link all object libraries.
# The executable is the one place that pulls in every concrete
# protocol library -- the upstream object libs only depend on the
# core integration lib (interfaces + manager). This keeps "what
# wire protocols does this binary speak" answerable from the
# link line.
target_link_libraries(industrial-hmi
    PRIVATE
        objectsModel
        objectsConfig
        objectsCore
        objectsAppGtk
        objectsPresenter
        objectsView
        objectsIntegrationCore
        objectsTcp
        objectsMqtt
        objectsMl
        objectsHistorian
        objectsAuth
        Threads::Threads
)
if(BUILD_OPCUA_BACKEND)
    target_link_libraries(industrial-hmi PRIVATE objectsOpcUa)
endif()
if(BUILD_MODBUS_BACKEND)
    target_link_libraries(industrial-hmi PRIVATE objectsModbus)
endif()

# Windows: Boost.Asio requires Winsock + mswsock (AcceptEx,
# GetAcceptExSockaddrs are in mswsock.lib, not ws2_32). StartupDialog
# needs user32 for MessageBoxW (usually auto-linked, listed here for
# clarity + safety).
if(WIN32)
    target_link_libraries(industrial-hmi PRIVATE ws2_32 mswsock user32)
endif()

# Make industrial-hmi pull the ML plugin into the build by dependency,
# WITHOUT linking it. The plugin is loaded at runtime via dlopen /
# LoadLibrary; pre-building it ensures the artefact sits next to the
# binary so the loader actually finds it on first inspection.
if(BUILD_ML_CLASSIFIER)
    add_dependencies(industrial-hmi industrial_ml_ort)
endif()

# Console Executable (Cross-Platform) -- shares src/main.cpp with the GTK
# binary via #ifdef CONSOLE_MODE. Zero gtkmm on the link line.
add_executable(industrial-hmi-console)
target_sources(industrial-hmi-console PRIVATE src/main.cpp)
target_compile_definitions(industrial-hmi-console PRIVATE CONSOLE_MODE)
target_link_libraries(industrial-hmi-console
    PRIVATE
        objectsModel
        objectsConfig
        objectsCore
        objectsPresenter
        objectsConsole
        objectsIntegrationCore
        objectsTcp
        objectsMqtt
        objectsMl
        objectsHistorian
        objectsAuth
        Threads::Threads
)
if(BUILD_OPCUA_BACKEND)
    target_link_libraries(industrial-hmi-console PRIVATE objectsOpcUa)
endif()
if(BUILD_MODBUS_BACKEND)
    target_link_libraries(industrial-hmi-console PRIVATE objectsModbus)
endif()
if(WIN32)
    # Boost.Asio's TCP server uses IOCP AcceptEx / GetAcceptExSockaddrs
    # from mswsock.lib (alongside ws2_32). user32 stays harmless even
    # though the console reporter never calls MessageBoxW.
    target_link_libraries(industrial-hmi-console PRIVATE ws2_32 mswsock user32)
endif()
if(Intl_FOUND)
    target_include_directories(industrial-hmi-console PRIVATE ${Intl_INCLUDE_DIRS})
    target_link_libraries(industrial-hmi-console PRIVATE ${Intl_LIBRARIES})
endif()
add_dependencies(industrial-hmi-console CopyAssets CopyConfig)
if(TARGET translations)
    add_dependencies(industrial-hmi-console translations)
endif()
target_compile_definitions(industrial-hmi-console PRIVATE
    LOCALE_DIR="${LOCALE_BUILD_DIR}"
)

# libintl - needed on platforms where gettext is not in libc (Windows, macOS)
if(Intl_FOUND)
    target_include_directories(industrial-hmi PRIVATE ${Intl_INCLUDE_DIRS})
    target_link_libraries(industrial-hmi PRIVATE ${Intl_LIBRARIES})
endif()

# Windows: Add resource file if exists (icon, version info)
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/resources/app.rc")
    target_sources(industrial-hmi PRIVATE resources/app.rc)
endif()

# Copy Assets to Build Directory (Cross-Platform)

# Use platform-agnostic path separators
add_custom_target(CopyAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/assets
    COMMENT "Copying assets to build directory"
)

add_custom_target(CopyConfig ALL
    # Nuke the runtime copy first so user-modified fields (e.g. the
    # last language chosen in Settings) don't survive a rebuild --
    # every build starts clean from the pristine source config.
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config
        ${CMAKE_BINARY_DIR}/config
    COMMENT "Copying config files to build directory"
)

# CopyUIFiles removed -- UI layouts and CSS now live under assets/ui and
# assets/styles, so CopyAssets already covers them.

add_dependencies(industrial-hmi CopyAssets CopyConfig)

# Translation Catalogs (.po -> .mo)
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

# Installation Rules (Cross-Platform)

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

# Tests (only when BUILD_TESTS=ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# CPack - Package Generation (Windows Installer / Linux Package)

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

# Summary (Cross-Platform)

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
message(STATUS "  objectsConfig          - INTERFACE: JSON config defaults + ConfigManager (policy)")
message(STATUS "  objectsModel           - Data layer (SimulatedModel, DatabaseManager)")
message(STATUS "  objectsCore            - Logger, i18n, Bootstrap, TimeFormat (GTK-free)")
message(STATUS "  objectsAuth            - Username/password auth + RBAC + audit + avatar placeholder (GTK-free)")
message(STATUS "  objectsMl              - Edge AI: ONNX inspection facade + plugin loader")
message(STATUS "  objectsIntegrationCore - Integration manager + serialisers + telemetry bridges")
message(STATUS "  objectsTcpBackend      - TCP wire backend (Boost.Asio)")
message(STATUS "  objectsMqtt            - MQTT 3.1.1 client + backend (Boost.Asio)")
message(STATUS "  objectsModbus          - Modbus TCP client + backend + ingest bridge")
message(STATUS "  objectsOpcUa           - OPC-UA server + client + bridge (open62541, optional)")
message(STATUS "  objectsAppGtk          - GTK application bootstrap (Application.cpp)")
message(STATUS "  objectsPresenter       - Presenter layer (MVP orchestration)")
message(STATUS "  objectsView            - View layer (GTK4 pages + dialogs + widgets)")
message(STATUS "  objectsConsole         - Headless console front-end (GTK-free)")
message(STATUS "")
message(STATUS "Dependency graph (low -> high):")
message(STATUS "  Config -> Model -> Core -> Auth   ----------+")
message(STATUS "                          \\-> IntegrationCore -+--> Presenter")
message(STATUS "                          \\-> Ml             --+")
message(STATUS "                                              |")
message(STATUS "                                              +-- View    -> industrial-hmi (GTK exe)")
message(STATUS "                                              +-- Console -> industrial-hmi-console")
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
