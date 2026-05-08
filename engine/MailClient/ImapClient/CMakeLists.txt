cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ImapClient STATIC)

target_sources(ImapClient PRIVATE
    src/ImapClient.cpp
)

target_include_directories(ImapClient PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ImapClient
    PUBLIC BaseClient
    PUBLIC ImapResponseHandling ImapCommand
)
