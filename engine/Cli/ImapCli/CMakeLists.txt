cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ImapCli STATIC)

target_sources(ImapCli PRIVATE 
    src/ImapCli.cpp
)

target_include_directories(ImapCli PUBLIC include)

target_link_libraries(ImapCli
    PUBLIC BaseCli
    PUBLIC ImapClient
    PUBLIC InMemoryMessageCache
    PUBLIC MailMessage
    PUBLIC MimeHandling
)
