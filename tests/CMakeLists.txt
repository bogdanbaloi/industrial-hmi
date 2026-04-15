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

# --- Presenter: BasePresenter (header-only observer mechanics) ------------
add_executable(test_base_presenter BasePresenterTest.cpp)
target_link_libraries(test_base_presenter PRIVATE
    ${TEST_COMMON_LIBS}
    tests_includes
)
add_test(NAME BasePresenterTest COMMAND test_base_presenter)

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

# Boost.Asio on Windows references WSAStartup / WSACleanup from Winsock;
# both objectsModel-linked targets need ws2_32.
if(WIN32)
    target_link_libraries(test_database_manager  PRIVATE ws2_32)
    target_link_libraries(test_products_presenter PRIVATE ws2_32)
endif()
