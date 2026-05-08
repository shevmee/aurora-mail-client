cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Consolidated MimeHandling library (combines MimeContext, MimeWriter, MimeReader)
add_library(MimeHandling STATIC
    MimeContext/src/MimeContext.cpp
    MimeWriter/src/MimeWriter.cpp
    MimeReader/src/MimeReader.cpp
    MimeReader/src/MimeParseError.cpp
)

target_include_directories(MimeHandling PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/MimeContext/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/MimeWriter/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/MimeReader/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(MimeHandling PUBLIC
    PkgConfig::gmime
    MailMessage
)
