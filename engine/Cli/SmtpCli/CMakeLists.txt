cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(SmtpCli STATIC)

target_sources(SmtpCli PRIVATE 
    src/SmtpCli.cpp
)

target_include_directories(SmtpCli PUBLIC include)

target_link_libraries(SmtpCli
    PUBLIC BaseCli
    PUBLIC SmtpClient
    PUBLIC SmtpCommand
    PUBLIC MailMessage
)
