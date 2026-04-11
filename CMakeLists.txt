cmake_minimum_required(VERSION 3.16)
project(industrial-hmi VERSION 1.0.0 LANGUAGES CXX)

# C++17 standard required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
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

# Optional: OPC-UA support
find_package(open62541 QUIET)
if(open62541_FOUND)
    add_compile_definitions(HAVE_OPCUA)
    message(STATUS "OPC-UA support enabled")
else()
    message(STATUS "OPC-UA support disabled (library not found)")
endif()

# Source files
set(PRESENTER_SOURCES
    src/presenter/BasePresenter.h
    src/presenter/BasePresenter.cpp
    src/presenter/DashboardPresenter.h
    src/presenter/DashboardPresenter.cpp
    src/presenter/ViewObserver.h
    src/presenter/modelview/EquipmentCardViewModel.h
    src/presenter/modelview/ActuatorCardViewModel.h
    src/presenter/modelview/ControlPanelViewModel.h
    src/presenter/modelview/WorkUnitViewModel.h
    src/presenter/modelview/PlaceholderViewModels.h
)

set(VIEW_SOURCES
    src/gtk/view/pages/DashboardPage.h
    src/gtk/view/pages/DashboardPage.cpp
)

set(MODEL_SOURCES
    src/model/SimulatedModel.h
)

# Main executable
add_executable(industrial-hmi
    src/main.cpp
    ${PRESENTER_SOURCES}
    ${VIEW_SOURCES}
    ${MODEL_SOURCES}
)

# Include directories
target_include_directories(industrial-hmi PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${GTKMM_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(industrial-hmi PRIVATE
    ${GTKMM_LIBRARIES}
    ${SQLITE_LIBRARIES}
    Boost::signals2
    Threads::Threads
)

if(open62541_FOUND)
    target_link_libraries(industrial-hmi PRIVATE open62541::open62541)
endif()

# Copy assets to build directory
file(COPY ${CMAKE_SOURCE_DIR}/assets 
     DESTINATION ${CMAKE_BINARY_DIR})

# Installation rules
install(TARGETS industrial-hmi DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/industrial-hmi/assets)

message(STATUS "")
message(STATUS "Industrial HMI Configuration:")
message(STATUS "  C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  GTK Version: ${GTKMM_VERSION}")
message(STATUS "  Boost Version: ${Boost_VERSION}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")
