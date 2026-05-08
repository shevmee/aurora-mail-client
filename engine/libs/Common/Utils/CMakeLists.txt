cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(Utils STATIC
    src/ImapUtf7.cpp
    src/Base64.cpp
)

target_include_directories(Utils 
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

find_package(Boost REQUIRED CONFIG COMPONENTS locale)

target_link_libraries(Utils PUBLIC 
    Boost::headers
    Boost::locale
)

# Build tests only if BUILD_TESTS is enabled and GTest is available
if(BUILD_TESTS AND GTest_FOUND)
    add_executable(UtilsTests 
        test/TestsBase64.cpp
    )

    target_compile_features(UtilsTests PRIVATE cxx_std_23)

    target_link_libraries(UtilsTests PRIVATE
        Utils
        GTest::gtest
        GTest::gtest_main
    )

    add_test(NAME UtilsTests COMMAND UtilsTests)
endif()
