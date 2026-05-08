cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(BaseCli STATIC)

target_sources(BaseCli PRIVATE
    src/BaseCli.cpp
)

target_include_directories(BaseCli PUBLIC include)

target_link_libraries(BaseCli PUBLIC
    Logging
    Configs
)
