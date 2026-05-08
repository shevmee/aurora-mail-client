cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(Base64 STATIC)

target_sources(Base64 PRIVATE src/Base64.cpp)

target_include_directories(Base64 PUBLIC include)

target_link_libraries(Base64 PUBLIC Boost::headers)

if(BUILD_TESTS AND GTest_FOUND)
    add_executable(Base64Tests test/TestsBase64.cpp)

    target_include_directories(Base64Tests INTERFACE include)

    target_compile_features(Base64Tests PRIVATE cxx_std_23)

    target_link_libraries(Base64Tests PRIVATE
        Base64
        GTest::gtest
        GTest::gtest_main
    )

    add_test(NAME Base64Tests COMMAND Base64Tests)
endif()
