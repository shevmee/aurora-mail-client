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

# Generate PDB symbols for Release builds. We deliberately keep /O2 + NDEBUG
# (so the binary is still optimised), but add /Zi to the compile step and
# /DEBUG /OPT:REF /OPT:ICF to the link step — the "shippable Release with
# symbols" combination. This is required for post-mortem crash analysis
# (WinDbg / Visual Studio / our scripts/resolve-crash.ps1) — without PDBs
# stack traces are just "module+0x<rva>".
if(MSVC)
    foreach(_lang C CXX)
        # /Zi  — full debug info into a separate PDB (no SBR pollution).
        # Avoid /Z7 because it bloats every .obj and slows incremental links.
        if(NOT CMAKE_${_lang}_FLAGS_RELEASE MATCHES "/Z[i7]")
            string(APPEND CMAKE_${_lang}_FLAGS_RELEASE " /Zi")
        endif()
    endforeach()
    foreach(_kind EXE SHARED MODULE)
        # /DEBUG     — emit the .pdb next to the binary.
        # /OPT:REF   — strip unreferenced functions/data (counter the size hit of /Zi).
        # /OPT:ICF   — fold identical COMDAT functions (shrinks Qt/Boost templated bloat).
        if(NOT CMAKE_${_kind}_LINKER_FLAGS_RELEASE MATCHES "/DEBUG")
            string(APPEND CMAKE_${_kind}_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
        endif()
    endforeach()
endif()

# Activate CTest at the root so `ctest` from the top-level build dir
# discovers test() calls in both engine/ and desktop/. The actual GTest
# discovery is gated on -DBUILD_TESTS=ON inside each subdir.
include(CTest)

add_subdirectory(engine)
add_subdirectory(desktop)

include(cmake/AuroraDevTools.cmake)
include(cmake/AuroraDocs.cmake)
