cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(MimeReader STATIC)

target_sources(MimeReader PRIVATE
    src/MimeReader.cpp
    src/MimeParseError.cpp
)

target_include_directories(MimeReader PUBLIC include)

target_link_libraries(MimeReader PUBLIC
    MimeContext
    MailMessage
)
