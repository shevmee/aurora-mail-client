cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(Logging STATIC)

target_sources(Logging
    PRIVATE
        src/Logger.cpp
        include/LogMessage.hpp
        include/Logger.hpp
        include/LoggerInstance.hpp
        include/LoggingPrimitives.hpp
)

target_include_directories(Logging PUBLIC include)

target_link_libraries(Logging PUBLIC
    Boost::headers
    Configs
)

if(BUILD_TESTS AND GTest_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test)
    add_executable(LoggingTests
        test/TestsLogMessage.cpp
    )

    target_compile_features(LoggingTests PRIVATE cxx_std_23)

    target_link_libraries(LoggingTests PRIVATE
        Logging
        GTest::gtest
        GTest::gtest_main
    )

    add_test(NAME LoggingTests COMMAND LoggingTests)
endif()
