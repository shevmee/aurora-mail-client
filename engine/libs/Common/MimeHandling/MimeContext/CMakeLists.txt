cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(MimeContext STATIC)

target_sources(MimeContext PRIVATE
    src/MimeContext.cpp
)

target_include_directories(MimeContext PUBLIC include)

target_link_libraries(MimeContext PUBLIC PkgConfig::gmime)
