cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Create library
add_library(ImapResponseHandling STATIC)

target_sources(ImapResponseHandling PRIVATE
    src/ImapParser.cpp
    src/ImapTokenizer.cpp
)

target_include_directories(ImapResponseHandling 
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Tests
if(BUILD_TESTS AND GTest_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    file(GLOB TEST_SOURCES "tests/*.cpp")
    if(TEST_SOURCES)
        add_executable(ImapResponseHandlingTests ${TEST_SOURCES})
        
        target_link_libraries(ImapResponseHandlingTests PRIVATE
            ImapResponseHandling
            GTest::gtest
            GTest::gtest_main
        )
        
        add_test(NAME ImapResponseHandlingTests COMMAND ImapResponseHandlingTests)
        
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(ImapResponseHandlingTests PRIVATE
                -fprofile-instr-generate
                -fcoverage-mapping
            )
            target_link_libraries(ImapResponseHandlingTests PRIVATE
                -fprofile-instr-generate
            )
        endif()
    endif()
endif()

