cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Create library
add_library(SmtpResponseHandling STATIC)

target_sources(SmtpResponseHandling PRIVATE
    src/Parser.cpp
)

target_include_directories(SmtpResponseHandling 
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Tests
if(BUILD_TESTS AND GTest_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    if(TEST_SOURCES)
        message(STATUS "Building SmtpResponseHandling tests: ${TEST_SOURCES}")
        add_executable(SmtpResponseHandlingTests ${TEST_SOURCES})
        
        target_link_libraries(SmtpResponseHandlingTests PRIVATE
            SmtpResponseHandling
            GTest::gtest
            GTest::gtest_main
        )
        
        add_test(NAME SmtpResponseHandlingTests COMMAND SmtpResponseHandlingTests)
        
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(SmtpResponseHandlingTests PRIVATE
                -fprofile-instr-generate
                -fcoverage-mapping
            )
            target_link_libraries(SmtpResponseHandlingTests PRIVATE
                -fprofile-instr-generate
            )
        endif()
    else()
        message(WARNING "No test sources found in ${CMAKE_CURRENT_SOURCE_DIR}/tests/")
    endif()
else()
    message(STATUS "SmtpResponseHandling tests disabled: BUILD_TESTS=${BUILD_TESTS}, GTest_FOUND=${GTest_FOUND}")
endif()

