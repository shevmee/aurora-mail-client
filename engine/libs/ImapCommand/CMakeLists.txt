cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Header-only library
add_library(ImapCommand INTERFACE)

target_include_directories(ImapCommand INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ImapCommand INTERFACE
    Utils
)

# Build tests only if BUILD_TESTS is enabled and GTest is available
if(BUILD_TESTS AND GTest_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/TestsImapCommand.cpp)
    add_executable(ImapCommandTests test/TestsImapCommand.cpp)

    target_include_directories(ImapCommandTests INTERFACE include)

    target_compile_features(ImapCommandTests PRIVATE cxx_std_23)

    target_link_libraries(ImapCommandTests PRIVATE 
        ImapCommand 
        GTest::gtest 
        GTest::gtest_main
    )

    add_test(NAME ImapCommandTests COMMAND ImapCommandTests)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(ImapCommandTests PRIVATE
            -fprofile-instr-generate
            -fcoverage-mapping
        )
        target_link_libraries(ImapCommandTests PRIVATE
            -fprofile-instr-generate
        )
    endif()
endif()
