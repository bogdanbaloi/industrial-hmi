# ============================================================================
# Unit Tests - GoogleTest + gmock
# ============================================================================
# Enabled via -DBUILD_TESTS=ON from the top-level CMakeLists.txt.
#
# Dependency resolution:
#   * Linux / MSYS2: find_package(GTest) via CMake config modules (both
#     libgtest-dev and mingw-w64-clang-x86_64-gtest ship them).
#   * Fallback for environments without GTest installed: pkg-config.
# ============================================================================

find_package(GTest CONFIG QUIET)

if(NOT GTest_FOUND)
    # Fallback: pkg-config (MSYS2 gtest ships a .pc file as well)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GTEST_PC QUIET gtest)
        pkg_check_modules(GMOCK_PC QUIET gmock)
    endif()

    if(NOT GTEST_PC_FOUND OR NOT GMOCK_PC_FOUND)
        message(FATAL_ERROR
            "BUILD_TESTS=ON but GoogleTest / gmock were not found.\n"
            "  MSYS2:  pacman -S mingw-w64-clang-x86_64-gtest\n"
            "  Ubuntu: apt install libgtest-dev libgmock-dev\n"
            "Or disable tests with -DBUILD_TESTS=OFF.")
    endif()

    # Create imported targets with the same names as the CMake config module
    # would expose, so test targets can link identically in both paths.
    add_library(GTest::gtest INTERFACE IMPORTED)
    target_include_directories(GTest::gtest INTERFACE ${GTEST_PC_INCLUDE_DIRS})
    target_link_libraries(GTest::gtest INTERFACE ${GTEST_PC_LIBRARIES})

    add_library(GTest::gtest_main INTERFACE IMPORTED)
    target_link_libraries(GTest::gtest_main INTERFACE GTest::gtest)
    target_link_libraries(GTest::gtest_main INTERFACE ${GTEST_PC_LIBRARIES})

    add_library(GTest::gmock INTERFACE IMPORTED)
    target_include_directories(GTest::gmock INTERFACE ${GMOCK_PC_INCLUDE_DIRS})
    target_link_libraries(GTest::gmock INTERFACE ${GMOCK_PC_LIBRARIES})
endif()

# ----------------------------------------------------------------------------
# Test executable - one binary per subsystem keeps link errors localized
# ----------------------------------------------------------------------------

# Common link set used by every test target
set(TEST_COMMON_LIBS
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

# Tiny INTERFACE target that just exposes the project source root so tests
# can include "src/..." paths without dragging in any of the object libs
# (and thus their transitive dependencies like GTK, Boost.Asio, etc).
add_library(tests_includes INTERFACE)
target_include_directories(tests_includes INTERFACE ${CMAKE_SOURCE_DIR})

# --- Core: Result<T, E> ---------------------------------------------------
# Result.h + ErrorHandling.h are header-only, so we avoid linking objectsCore
# (which would pull in Application.cpp -> MainWindow, Boost.Asio -> Winsock).
add_executable(test_result ResultTest.cpp)
target_link_libraries(test_result PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
)
add_test(NAME ResultTest COMMAND test_result)

# --- Config: ConfigManager ------------------------------------------------
# ConfigManager.h is header-only (pure std:: code) and config_defaults.h is a
# plain constexpr header; neither needs GTK or the model layer.
add_executable(test_config_manager ConfigManagerTest.cpp)
target_link_libraries(test_config_manager PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
)
# Tests write temp config files; run with the source dir as working dir
# so fixtures can place files relative to CMAKE_BINARY_DIR without surprises.
add_test(NAME ConfigManagerTest COMMAND test_config_manager
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# --- Model: DatabaseManager ------------------------------------------------
# DatabaseManager.cpp uses Boost.Asio (async helpers) and SQLite at runtime,
# so we do need the real object lib + its transitive deps here.
add_executable(test_database_manager DatabaseManagerTest.cpp)
target_link_libraries(test_database_manager PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
    objectsModel
)
add_test(NAME DatabaseManagerTest COMMAND test_database_manager)

# --- Core: CsvSerializer (pure C++, no GTK) --------------------------------
add_executable(test_csv_serializer CsvSerializerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/CsvSerializer.cpp)
target_link_libraries(test_csv_serializer PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
)
add_test(NAME CsvSerializerTest COMMAND test_csv_serializer)

# --- Model: SimulatedModel (singleton, no GTK) ----------------------------
add_executable(test_simulated_model SimulatedModelTest.cpp)
target_link_libraries(test_simulated_model PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
)
add_test(NAME SimulatedModelTest COMMAND test_simulated_model)

# --- Core: Logger (CapturingLogger + helpers) ------------------------------
add_executable(test_logger LoggerTest.cpp)
target_link_libraries(test_logger PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
)
add_test(NAME LoggerTest COMMAND test_logger)

# --- Core: StartupErrors (header-only typed exception hierarchy) ----------
add_executable(test_startup_errors StartupErrorsTest.cpp)
target_link_libraries(test_startup_errors PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
)
add_test(NAME StartupErrorsTest COMMAND test_startup_errors)

# --- Core: ExceptionHandler (StandardExceptionHandler + safeExecute +
#     ExceptionGuard). Links NullLogger from LoggerImpl.h only — no GTK.
add_executable(test_exception_handler ExceptionHandlerTest.cpp)
target_link_libraries(test_exception_handler PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
)
add_test(NAME ExceptionHandlerTest COMMAND test_exception_handler)

# --- Core: StartupDialog (console-mode path only; MessageBoxW on Windows
#     is exercised manually + via scenario suite). Compiles the source
#     directly so the test stays on a minimal GTK-free link line.
add_executable(test_startup_dialog
    StartupDialogTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/StartupDialog.cpp)
target_link_libraries(test_startup_dialog PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
)
if(WIN32)
    target_link_libraries(test_startup_dialog PRIVATE user32)
endif()
add_test(NAME StartupDialogTest COMMAND test_startup_dialog
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# --- Presenter: BasePresenter (header-only observer mechanics) ------------
add_executable(test_base_presenter BasePresenterTest.cpp)
target_link_libraries(test_base_presenter PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
)
add_test(NAME BasePresenterTest COMMAND test_base_presenter)

# --- Presenter: AlertCenter (header-only, sigc++ provides the signal) ----
# Pulls GTKMM's include dirs to find <sigc++/signal.h>; GTKMM transitively
# re-exports sigc++-3.0 headers so we don't need a separate find_package.
add_executable(test_alert_center AlertCenterTest.cpp)
target_link_libraries(test_alert_center PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
    ${GTKMM_LIBRARIES}
)
target_include_directories(test_alert_center PRIVATE ${GTKMM_INCLUDE_DIRS})
add_test(NAME AlertCenterTest COMMAND test_alert_center)

# --- Presenter: ProductsPresenter (with mock repository) ------------------
# ProductsPresenter.cpp lives in objectsPresenter; MockProductsRepository
# is a header in tests/mocks. The test target needs the source root on its
# include path (tests_includes) plus objectsPresenter (and transitively
# objectsModel for DatabaseManager symbols referenced in the .cpp).
add_executable(test_products_presenter ProductsPresenterTest.cpp)
target_include_directories(test_products_presenter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}  # so #include "mocks/..." resolves
)
target_link_libraries(test_products_presenter PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
    objectsPresenter
    objectsModel
)

add_test(NAME ProductsPresenterTest COMMAND test_products_presenter)

# --- Presenter: ProductsPresenter ASYNC CRUD -----------------------------
# Exercises addProduct / updateProduct / deleteProduct / exportProducts,
# each of which posts to the Boost.Asio I/O thread in ModelContext and
# marshals back via Glib::signal_idle. Test drives a Glib::MainLoop to
# pump the idle queue so callbacks actually run under gtest.
#
# Links GTKMM for glibmm (same pattern as AlertCenterTest). Links
# objectsModel for DatabaseManager + ModelContext + Boost.Asio.
add_executable(test_products_presenter_async ProductsPresenterAsyncTest.cpp)
target_include_directories(test_products_presenter_async PRIVATE
    ${GTKMM_INCLUDE_DIRS}
)
target_link_libraries(test_products_presenter_async PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
    objectsPresenter
    objectsModel
    ${GTKMM_LIBRARIES}
)
add_test(NAME ProductsPresenterAsyncTest COMMAND test_products_presenter_async)

# --- Presenter: DashboardPresenter (with mock production model) -----------
# DashboardPresenter.cpp pulls in SimulatedModel via its default
# constructor's initializer list, so we still need objectsModel for the
# default-ctor instantiation, even though the test injects a mock.
add_executable(test_dashboard_presenter DashboardPresenterTest.cpp)
target_include_directories(test_dashboard_presenter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}  # so #include "mocks/..." resolves
)
target_link_libraries(test_dashboard_presenter PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
    objectsPresenter
    objectsModel
)

add_test(NAME DashboardPresenterTest COMMAND test_dashboard_presenter)

# ============================================================================
# View-layer tests (require GTK initialised via custom main)
# ============================================================================
# These binaries use ViewTestMain.cpp instead of gtest_main, because
# gtk_init() must run before any widget is constructed. On Linux CI the
# test command is prefixed with xvfb-run (see ci.yml).

set(VIEW_COMMON_LIBS
    ${TEST_COMMON_LIBS}
    tests_includes
    objectsView
    objectsAppGtk      # Application.cpp moved here out of objectsCore
    objectsPresenter
    objectsModel
    objectsCore
    objectsConfig
    Threads::Threads
)

# --- View: DashboardPage --------------------------------------------------
add_executable(test_dashboard_page
    DashboardPageTest.cpp
    ViewTestMain.cpp          # custom main with gtk_init()
)
target_include_directories(test_dashboard_page PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_dashboard_page PRIVATE ${VIEW_COMMON_LIBS})
# Remove gtest_main since we supply our own main()
target_link_libraries(test_dashboard_page PRIVATE GTest::gtest GTest::gmock)
add_test(NAME DashboardPageTest COMMAND test_dashboard_page
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# --- View: ProductsPage ---------------------------------------------------
add_executable(test_products_page
    ProductsPageTest.cpp
    ViewTestMain.cpp
)
target_include_directories(test_products_page PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_products_page PRIVATE ${VIEW_COMMON_LIBS})
target_link_libraries(test_products_page PRIVATE GTest::gtest GTest::gmock)
add_test(NAME ProductsPageTest COMMAND test_products_page
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# --- View: DialogManager --------------------------------------------------
# Exercises every public method on DialogManager under a real GTK main
# loop (gtk_init in ViewTestMain.cpp). Blocking variants (showConfirm,
# showInput, showForm) spin their own inner Glib::MainLoop — the tests
# pre-arm a signal_timeout that dispatches Dialog::response() once the
# inner loop starts pumping.
add_executable(test_dialog_manager
    DialogManagerTest.cpp
    ViewTestMain.cpp
)
target_include_directories(test_dialog_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_dialog_manager PRIVATE ${VIEW_COMMON_LIBS})
target_link_libraries(test_dialog_manager PRIVATE GTest::gtest GTest::gmock)
add_test(NAME DialogManagerTest COMMAND test_dialog_manager
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Boost.Asio on Windows references WSAStartup / WSACleanup from Winsock;
# every objectsModel-linked target needs ws2_32.
if(WIN32)
    target_link_libraries(test_database_manager          PRIVATE ws2_32)
    target_link_libraries(test_products_presenter        PRIVATE ws2_32)
    target_link_libraries(test_products_presenter_async  PRIVATE ws2_32)
    target_link_libraries(test_dashboard_presenter       PRIVATE ws2_32)
    target_link_libraries(test_dashboard_page            PRIVATE ws2_32)
    target_link_libraries(test_products_page             PRIVATE ws2_32)
    target_link_libraries(test_dialog_manager            PRIVATE ws2_32)
endif()

# ============================================================================
# Console scenario tests — end-to-end MVP regression suite
# ============================================================================
# Each *.txt scenario is piped into industrial-hmi-console and the filtered
# stdout is diffed against the matching *.expected file. The runner
# (tests/scenarios/run-scenario.cmake) normalises line endings + strips
# logger timestamp lines so only structural events and command output
# participate in the match.
#
# No display server, no Xvfb, no GLib main loop — these run on any CI
# runner and give us cheap cross-platform regression coverage of the
# full Model -> Presenter -> View pipeline.
set(CONSOLE_SCENARIOS
    boot-and-quit
    status-snapshot
    start-stop-cycle
    equipment-toggle
    products-list
    help-and-unknown
    alerts-and-dismiss
    view-product
    error-paths
    full-cycle
)

foreach(scenario ${CONSOLE_SCENARIOS})
    add_test(
        NAME Scenario_${scenario}
        COMMAND ${CMAKE_COMMAND}
            "-DCONSOLE_BIN=$<TARGET_FILE:industrial-hmi-console>"
            "-DSCENARIO_INPUT=${CMAKE_SOURCE_DIR}/tests/scenarios/${scenario}.txt"
            "-DSCENARIO_EXPECTED=${CMAKE_SOURCE_DIR}/tests/scenarios/${scenario}.expected"
            "-DSCENARIO_NAME=${scenario}"
            -P ${CMAKE_SOURCE_DIR}/tests/scenarios/run-scenario.cmake
        # CWD = build dir so the console binary picks up the build-dir
        # config + locale layout, same as running it directly from there.
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endforeach()
