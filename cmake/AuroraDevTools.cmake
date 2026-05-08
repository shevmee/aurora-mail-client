# AuroraMail developer tools: clang-format, clang-tidy and clazy on repository
# sources only (engine/ and desktop/), not system or package headers.
#
# Configure with a top-level build directory so compile_commands.json covers all targets:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build --target aurora-clang-format-check
#   cmake --build build --target aurora-clang-tidy
#   cmake --build build --target aurora-clazy
#
# clang-tidy and clazy both require compile_commands.json
# (enabled by CMAKE_EXPORT_COMPILE_COMMANDS).

find_program(AURORA_CLANG_FORMAT NAMES clang-format)
find_program(AURORA_CLANG_TIDY NAMES clang-tidy)
find_program(AURORA_RUN_CLANG_TIDY NAMES run-clang-tidy run-clang-tidy.py)
find_program(AURORA_CLAZY NAMES clazy-standalone clazy)

# All C/C++ sources under engine/ and desktop/ (repository paths only).
file(
    GLOB_RECURSE _aurora_dev_sources
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/engine/**/*.cpp"
    "${CMAKE_SOURCE_DIR}/engine/**/*.c"
    "${CMAKE_SOURCE_DIR}/engine/**/*.h"
    "${CMAKE_SOURCE_DIR}/engine/**/*.hpp"
    "${CMAKE_SOURCE_DIR}/desktop/**/*.cpp"
    "${CMAKE_SOURCE_DIR}/desktop/**/*.c"
    "${CMAKE_SOURCE_DIR}/desktop/**/*.h"
    "${CMAKE_SOURCE_DIR}/desktop/**/*.hpp"
    "${CMAKE_SOURCE_DIR}/desktop/**/*.mm"
)

# Drop accidental matches if any tool walks into nested build trees inside the repo.
set(_aurora_dev_sources_filtered "")
foreach(_f IN LISTS _aurora_dev_sources)
    if(_f MATCHES "[/\\\\](build|cmake-build-[^/\\\\]*|\\.git)[/\\\\]")
        continue()
    endif()
    list(APPEND _aurora_dev_sources_filtered "${_f}")
endforeach()
set(AURORA_DEV_SOURCES "${_aurora_dev_sources_filtered}")

if(AURORA_CLANG_FORMAT)
    add_custom_target(
        aurora-clang-format
        COMMAND "${AURORA_CLANG_FORMAT}" -i ${AURORA_DEV_SOURCES}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "clang-format -i (engine/ and desktop/)"
        VERBATIM
    )

    add_custom_target(
        aurora-clang-format-check
        COMMAND "${AURORA_CLANG_FORMAT}" --dry-run --Werror ${AURORA_DEV_SOURCES}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "clang-format --dry-run --Werror (engine/ and desktop/)"
        VERBATIM
    )
else()
    message(STATUS "clang-format not found; aurora-clang-format targets disabled")
endif()

# Prefer LLVM's run-clang-tidy: uses compile_commands.json and -files-regex (only our tree).
# Do not test EXISTS(compile_commands.json) at configure time — CMake may write it after this file is processed.
if(AURORA_RUN_CLANG_TIDY)
    # Only sources under the repository's engine/ and desktop/ (not build/.../desktop/ autogen).
    set(_aurora_root "${CMAKE_SOURCE_DIR}")
    cmake_path(NORMAL_PATH _aurora_root)
    string(REGEX REPLACE "([][+.*()?^${}|\\\\])" "\\\\\\1" _aurora_src_esc "${_aurora_root}")
    set(_aurora_source_filter "^${_aurora_src_esc}[/\\\\](engine|desktop)[/\\\\].*\\.(c|cc|cpp|cxx|h|hpp|mm)$")

    set(_aurora_tidy_cmd "${AURORA_RUN_CLANG_TIDY}" -p "${CMAKE_BINARY_DIR}" -config-file=${CMAKE_SOURCE_DIR}/.clang-tidy
        -quiet "-source-filter=${_aurora_source_filter}")

    if(APPLE)
        execute_process(
            COMMAND xcrun --show-sdk-path
            OUTPUT_VARIABLE _aurora_macos_sdk
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_aurora_macos_sdk)
            # Helps clang-tidy find libc++ headers (e.g. <cstdint>) on macOS.
            list(APPEND _aurora_tidy_cmd "-extra-arg=-isysroot${_aurora_macos_sdk}")
        endif()
    endif()

    add_custom_target(aurora-clang-tidy COMMAND ${_aurora_tidy_cmd} WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                      COMMENT "run-clang-tidy (engine/ and desktop/ only)" VERBATIM)
elseif(AURORA_CLANG_TIDY)
    # Fallback: invoke clang-tidy per file (slower; stops at first failure).
    set(_script "${CMAKE_BINARY_DIR}/aurora-clang-tidy-batch.cmake")
    set(_batch_content "")
    foreach(_f IN LISTS AURORA_DEV_SOURCES)
        string(
            APPEND
            _batch_content
            "execute_process(COMMAND \"${AURORA_CLANG_TIDY}\" -p \"${CMAKE_BINARY_DIR}\" --config-file=\"${CMAKE_SOURCE_DIR}/.clang-tidy\" \"${_f}\" RESULT_VARIABLE _r)\nif(NOT _r EQUAL 0)\n  message(FATAL_ERROR \"clang-tidy failed: ${_f}\")\nendif()\n"
        )
    endforeach()
    file(WRITE "${_script}" "${_batch_content}")
    add_custom_target(
        aurora-clang-tidy COMMAND "${CMAKE_COMMAND}" -P "${_script}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "clang-tidy per file (install LLVM run-clang-tidy for faster runs)"
        VERBATIM
    )
else()
    message(STATUS "clang-tidy not found; aurora-clang-tidy target disabled (install LLVM)")
endif()

# clazy: Qt-aware static analyzer built on top of Clang. Distributed as
# clazy-standalone (compatible with compile_commands.json) and via apt/brew/pacman.
#
# Default level (1+2) is enabled; project-specific suppressions can be moved
# into .clazy-checks at the repository root if/when needed.
if(AURORA_CLAZY)
    set(_aurora_clazy_checks "level1,level2")

    # Filter the same way as clang-tidy: only repository sources under engine/ and desktop/.
    set(_aurora_clazy_sources_filtered "")
    foreach(_f IN LISTS AURORA_DEV_SOURCES)
        # Skip Objective-C++ — clazy is a Clang plugin tuned for Qt/C++ TUs only.
        if(_f MATCHES "\\.mm$")
            continue()
        endif()
        list(APPEND _aurora_clazy_sources_filtered "${_f}")
    endforeach()

    set(_clazy_script "${CMAKE_BINARY_DIR}/aurora-clazy-batch.cmake")
    set(_clazy_batch_content "")
    foreach(_f IN LISTS _aurora_clazy_sources_filtered)
        string(
            APPEND
            _clazy_batch_content
            "execute_process(COMMAND \"${AURORA_CLAZY}\" -p \"${CMAKE_BINARY_DIR}\" -checks=${_aurora_clazy_checks} \"${_f}\" RESULT_VARIABLE _r)\nif(NOT _r EQUAL 0)\n  message(FATAL_ERROR \"clazy failed: ${_f}\")\nendif()\n"
        )
    endforeach()
    file(WRITE "${_clazy_script}" "${_clazy_batch_content}")
    add_custom_target(
        aurora-clazy COMMAND "${CMAKE_COMMAND}" -P "${_clazy_script}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "clazy ${_aurora_clazy_checks} (engine/ and desktop/ only)"
        VERBATIM
    )
else()
    message(STATUS "clazy/clazy-standalone not found; aurora-clazy target disabled (install clazy)")
endif()
