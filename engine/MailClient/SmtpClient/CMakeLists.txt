cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(SmtpClient STATIC)

target_sources(SmtpClient PRIVATE 
    src/SmtpClient.cpp
)

target_include_directories(SmtpClient PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(SmtpClient
    PUBLIC BaseClient
    PUBLIC SmtpResponseHandling SmtpCommand
    PUBLIC MimeHandling
)
