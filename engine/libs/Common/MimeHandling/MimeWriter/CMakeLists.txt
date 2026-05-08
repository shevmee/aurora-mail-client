cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(MimeWriter STATIC)

target_sources(MimeWriter PRIVATE
    src/MimeWriter.cpp
)

target_include_directories(MimeWriter PUBLIC include)

target_link_libraries(MimeWriter PUBLIC
    MimeContext
    MailMessage
)
