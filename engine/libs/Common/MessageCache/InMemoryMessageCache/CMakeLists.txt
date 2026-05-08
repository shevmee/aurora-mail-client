cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(InMemoryMessageCache STATIC)

target_sources(InMemoryMessageCache PRIVATE
    src/InMemoryMessageCache.cpp
)

target_include_directories(InMemoryMessageCache PUBLIC include)

target_link_libraries(InMemoryMessageCache PUBLIC
    IMessageCache
)
