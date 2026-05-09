# Desktop unit tests. Activated when -DBUILD_TESTS=ON is passed at the top
# level (the same flag the engine module defines). The tests link against
# AuroraMailCore -- the Qt-based static library that owns all of the
# desktop's non-UI logic (config, email parsing, message cache).
#
# We deliberately do NOT pull in any of the Ui targets here: the UI is built
# around QWidget + QApplication and is exercised through manual smoke tests
# instead. The Core targets are pure data/services and are unit-testable
# without an event loop.
if(NOT BUILD_TESTS)
    return()
endif()

# `find_package(GTest)` was already invoked in engine/CMakeLists.txt, but the
# resulting target/find variables are scoped to that subdirectory. Re-run the
# find here -- find_package is idempotent and just picks up the cached vars.
find_package(GTest QUIET)
if(NOT GTest_FOUND OR NOT TARGET GTest::gtest)
    message(STATUS "DesktopCoreTests: GTest not found, skipping")
    return()
endif()

set(_DESKTOP_TEST_SOURCES
    DesktopTestMain.cpp
    TestsMessageKey.cpp
    TestsTextSanitizer.cpp
    TestsCachedMessage.cpp
    TestsAesGcmCipher.cpp
    TestsAppConfig.cpp
    TestsMemoryMessageCache.cpp
    TestsEmailParser.cpp
)

add_executable(DesktopCoreTests ${_DESKTOP_TEST_SOURCES})

target_compile_features(DesktopCoreTests PRIVATE cxx_std_23)

target_link_libraries(DesktopCoreTests PRIVATE
    AuroraMailCore
    AuroraMailEngine
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Sql
    GTest::gtest
    # Note: we register our own main (DesktopTestMain.cpp) to construct a
    # QCoreApplication before any test runs, so we deliberately do NOT link
    # GTest::gtest_main here.
)

add_test(NAME DesktopCoreTests COMMAND DesktopCoreTests)
