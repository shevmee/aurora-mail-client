# Mail protocol engine (IMAP/SMTP, MIME, …). Built via AuroraMail root CMakeLists.txt.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options - MUST be before subdirectories
option(BUILD_TESTS "Build unit tests" OFF)

# CMake function to handle find_package results consistently
function(handle_package_find PACKAGE_NAME IS_REQUIRED)
    set(FOUND_VAR "${PACKAGE_NAME}_FOUND")
    
    if(${FOUND_VAR})
        message(STATUS "Found ${PACKAGE_NAME}")
        
        # Special handling for specific packages
        if(PACKAGE_NAME STREQUAL "GTest")
            message(STATUS "  └── Version: ${GTEST_VERSION}")
            enable_testing()
        elseif(PACKAGE_NAME STREQUAL "OpenSSL")
            message(STATUS "  └── Version: ${OPENSSL_VERSION}")
        elseif(PACKAGE_NAME STREQUAL "Boost")
            message(STATUS "  └── Version: ${Boost_VERSION}")
        elseif(PACKAGE_NAME STREQUAL "Threads")
            message(STATUS "  └── Threading library found")
        elseif(PACKAGE_NAME STREQUAL "nlohmann_json")
            message(STATUS "  └── Version: ${nlohmann_json_VERSION}")
        endif()
    else()
        if(IS_REQUIRED)
            message(FATAL_ERROR "${PACKAGE_NAME} not found and is required.")
        else()
            message(WARNING "${PACKAGE_NAME} not found (optional)")
        endif()
    endif()
endfunction()

find_package(OpenSSL REQUIRED)
handle_package_find("OpenSSL" TRUE)

find_package(Boost 1.83.0 REQUIRED CONFIG COMPONENTS headers)
handle_package_find("Boost" TRUE)

find_package(Threads REQUIRED)
handle_package_find("Threads" TRUE)

# nlohmann_json is now an *optional* engine-tree dependency — only the CLI
# tool (aurora-mail-cli) loads its config from JSON. The engine library
# itself (`AuroraMailEngine` interface) is JSON-agnostic; the desktop app
# uses Qt's QJsonDocument family for all of its JSON serialization (config,
# tokens, account registry, AI REST) and no longer links nlohmann_json.
find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
    handle_package_find("nlohmann_json" FALSE)
endif()

# Find GTest if tests are enabled - MUST be before subdirectories
if(BUILD_TESTS)
    find_package(GTest REQUIRED)
    handle_package_find("GTest" TRUE)
    include(CTest)
    enable_testing()
endif()

# GMime 3.x: prefer pkg-config (Linux, macOS Homebrew, vcpkg with PKG_CONFIG_PATH).
# Fallback: find_path/find_library for MSVC or minimal installs.
find_package(PkgConfig QUIET)
set(_AURORA_GMIME_RESOLVED FALSE)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_GMIME QUIET gmime-3.0)
    if(PC_GMIME_FOUND)
        add_library(PkgConfig::gmime INTERFACE IMPORTED)
        set_target_properties(PkgConfig::gmime PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${PC_GMIME_INCLUDE_DIRS}"
            INTERFACE_LINK_DIRECTORIES "${PC_GMIME_LIBRARY_DIRS}"
            INTERFACE_LINK_LIBRARIES "${PC_GMIME_LIBRARIES}"
            INTERFACE_COMPILE_OPTIONS "${PC_GMIME_CFLAGS_OTHER}"
        )
        set(_AURORA_GMIME_RESOLVED TRUE)
    endif()
endif()
if(NOT _AURORA_GMIME_RESOLVED)
    find_path(AURORA_GMIME_INCLUDE_DIR NAMES gmime/gmime.h
        PATH_SUFFIXES gmime-3.0
        DOC "GMime 3 headers (directory containing gmime/gmime.h)")
    find_library(AURORA_GMIME_LIBRARY
        NAMES gmime-3.0 gmime-3 gmime libgmime-3.0 libgmime-3 libgmime-3.0-0
        DOC "GMime 3 library")
    if(NOT AURORA_GMIME_INCLUDE_DIR OR NOT AURORA_GMIME_LIBRARY)
        message(FATAL_ERROR
            "GMime 3.x not found.\n"
            "  • Debian/Ubuntu: libgmime-3.0-dev\n"
            "  • Fedora: gmime-devel\n"
            "  • macOS: brew install gmime\n"
            "  • Windows: vcpkg install gmime, then -DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake\n"
            "  Or set AURORA_GMIME_INCLUDE_DIR and AURORA_GMIME_LIBRARY.")
    endif()
    add_library(PkgConfig::gmime INTERFACE IMPORTED)
    set_target_properties(PkgConfig::gmime PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${AURORA_GMIME_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${AURORA_GMIME_LIBRARY}"
    )
endif()

# Single-configuration generators only (Ninja/Make); VS/Xcode use CMAKE_CONFIGURATION_TYPES.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

add_subdirectory(libs/Common)
add_subdirectory(libs/ResponseHandling)
add_subdirectory(libs/ImapCommand)
add_subdirectory(libs/SmtpCommand)
add_subdirectory(MailClient)
add_subdirectory(Cli)

# Aggregate interface library for the desktop app and tools
add_library(AuroraMailEngine INTERFACE)

target_link_libraries(AuroraMailEngine
    INTERFACE
    SmtpClient
    ImapClient
    Common
    ResponseHandling
    ImapCommand
    SmtpCommand

    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::headers
    Threads::Threads
    PkgConfig::gmime
)
# Note: AuroraMailEngine no longer re-exports nlohmann_json. Consumers that
# need JSON (e.g. the desktop's AppConfig loader, or the CLI binary below)
# link nlohmann_json::nlohmann_json explicitly.

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunused -Wunused-function -Wunused-label")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunused-parameter -Wunused-value -Wunused-variable")
elseif(MSVC)
    # Equivalent warning level for cl.exe / clang-cl on Windows.
    # /utf-8 is needed to compile files containing UTF-8 string literals
    # (Ukrainian UI strings, comments) on Windows code pages other than 65001.
    add_compile_options(/W4 /utf-8)
endif()

# Executable target configuration. The CLI binary is the only engine-side
# artifact that talks JSON, so nlohmann_json is linked here (and only here).
if(NOT nlohmann_json_FOUND)
    message(FATAL_ERROR
        "aurora-mail-cli requires nlohmann_json (used to parse its config_example.json)."
        " Install it (Debian/Ubuntu: nlohmann-json3-dev, macOS: brew install nlohmann-json,"
        " vcpkg install nlohmann-json) or pass -DCMAKE_DISABLE_FIND_PACKAGE_nlohmann_json=ON"
        " and skip the CLI target.")
endif()
add_executable(aurora-mail-cli main.cpp)
target_link_libraries(aurora-mail-cli PRIVATE
    AuroraMailEngine
    ImapCli
    SmtpCli
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# clang-format / clang-tidy / clazy: use top-level targets from
# cmake/AuroraDevTools.cmake (repository root):
#   cmake --build <build> --target aurora-clang-format
#   cmake --build <build> --target aurora-clang-format-check
#   cmake --build <build> --target aurora-clang-tidy
#   cmake --build <build> --target aurora-clazy

