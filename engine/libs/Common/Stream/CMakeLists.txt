cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(Stream STATIC)

target_sources(Stream PRIVATE src/Stream.cpp)

target_include_directories(Stream PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(Stream PUBLIC
    Boost::headers
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Build tests only if BUILD_TESTS is enabled and GTest is available
if(BUILD_TESTS AND GTest_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/TestsStream.cpp)
    add_executable(StreamTests test/TestsStream.cpp)

    target_include_directories(StreamTests INTERFACE include)

    target_compile_features(StreamTests PRIVATE cxx_std_23)

    target_link_libraries(StreamTests PRIVATE
        Stream
        GTest::gtest
        GTest::gtest_main
    )
    
    add_test(NAME StreamTests COMMAND StreamTests)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(StreamTests PRIVATE
            -fprofile-instr-generate
            -fcoverage-mapping
        )
        target_link_libraries(StreamTests PRIVATE
            -fprofile-instr-generate
        )
    endif()
endif()