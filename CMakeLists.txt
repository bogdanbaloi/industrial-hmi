cmake_minimum_required(VERSION 3.16)
project(industrial-hmi VERSION 1.0.0 DESCRIPTION "Industrial HMI MVP Architecture" LANGUAGES CXX)

# C++17 standard required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-unused-parameter
    )
endif()

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-4.0)
pkg_check_modules(SQLITE REQUIRED sqlite3)

find_package(Boost REQUIRED COMPONENTS signals2)
find_package(Threads REQUIRED)

# ============================================================================
# Object Libraries (Modular Build Pattern)
# ============================================================================

# Model Layer - Data and business logic simulation (header-only)
add_library(objectsModel INTERFACE)
target_sources(objectsModel
    INTERFACE
        FILE_SET HEADERS
        FILES
            src/model/SimulatedModel.h
            src/model/DatabaseManager.h
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
        src/gtk/view/pages/DashboardPage.cpp
        src/gtk/view/pages/ProductsPage.cpp
    PRIVATE
        FILE_SET internalHeaders
        TYPE HEADERS
        FILES
            src/gtk/view/MainWindow.h
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
# Main Executable
# ============================================================================

add_executable(industrial-hmi)
target_sources(industrial-hmi
    PRIVATE
        src/main.cpp
)

# Link all object libraries
target_link_libraries(industrial-hmi
    PRIVATE
        objectsModel
        objectsPresenter
        objectsView
        Threads::Threads
)

# ============================================================================
# Copy Assets to Build Directory
# ============================================================================

add_custom_target(CopyAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/assets
    COMMENT "Copying assets to build directory"
)

add_custom_target(CopyUIFiles ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/ui
        ${CMAKE_BINARY_DIR}/ui
    COMMENT "Copying UI files to build directory"
)

add_dependencies(industrial-hmi CopyAssets CopyUIFiles)

# ============================================================================
# Installation Rules
# ============================================================================

install(TARGETS industrial-hmi
    RUNTIME DESTINATION bin
)

install(DIRECTORY assets
    DESTINATION share/industrial-hmi
)

install(DIRECTORY ui
    DESTINATION share/industrial-hmi
)

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "Industrial HMI Configuration Summary:")
message(STATUS "  Version:      ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler:     ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  GTK4:         ${GTKMM_VERSION}")
message(STATUS "  SQLite:       ${SQLITE_VERSION}")
message(STATUS "")
message(STATUS "Object Libraries:")
message(STATUS "  objectsModel      - Data layer (SimulatedModel, DatabaseManager)")
message(STATUS "  objectsPresenter  - Presenter layer (MVP orchestration)")
message(STATUS "  objectsView       - View layer (GTK4 pages)")
message(STATUS "")
