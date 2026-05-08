cmake_minimum_required(VERSION 3.22)
# Prefer Config-mode Boost (silences FindBoost deprecation on CMake 3.30+).
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# CMake 3.22+ is widely packaged (Ubuntu 22.04+, current Visual Studio, Homebrew).
project(AuroraMail
    VERSION 1.0.0
    DESCRIPTION "Aurora Mail — desktop client and mail engine"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# compile_commands.json for clang-tidy (also set in engine/, duplicated for clarity at top level).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(engine)
add_subdirectory(desktop)

include(cmake/AuroraDevTools.cmake)
