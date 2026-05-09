cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Pure data + helpers. JSON deserialization for these structs lives in the
# consumers (desktop/src/Core/Config and engine/main.cpp), so the engine
# library tree no longer needs nlohmann_json on its public link line.
add_library(Configs STATIC)

target_sources(Configs PRIVATE src/StartupConfig.cpp)

target_include_directories(Configs PUBLIC include)

target_include_directories(Configs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../Logging/include  # For LoggingPrimitives.hpp only
)

# JSON-parsing tests moved with the parsers themselves:
#   * desktop/src/Core/Config (AppConfig + JSON loader, tested as part of the
#     desktop test suite once it is wired into BUILD_TESTS).
#   * engine/main.cpp inline loader is exercised by the existing CLI smoke tests.
#
# Pure-data tests (port helpers, ConnectionMode formatting) live with the
# struct definitions and run as ConfigsTests.
if(BUILD_TESTS AND GTest_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/TestsStartupConfig.cpp)
    add_executable(ConfigsTests test/TestsStartupConfig.cpp)

    target_compile_features(ConfigsTests PRIVATE cxx_std_23)

    target_link_libraries(ConfigsTests PRIVATE
        Configs
        GTest::gtest
        GTest::gtest_main
    )

    add_test(NAME ConfigsTests COMMAND ConfigsTests)
endif()
