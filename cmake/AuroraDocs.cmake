# AuroraMail Doxygen documentation targets.
#
# Adds three convenience targets when doxygen is on PATH:
#
#   aurora-docs-engine   -- build engine/docs/html
#   aurora-docs-desktop  -- build desktop/docs/html
#   aurora-docs          -- meta-target that depends on both
#
# Usage (from a configured top-level build directory, e.g. build/macos-debug):
#
#   cmake --build . --target aurora-docs
#   open  ../../engine/docs/html/index.html      # macOS
#   open  ../../desktop/docs/html/index.html
#
# Doxygen and Graphviz must be installed (brew install doxygen graphviz on
# macOS, apt install doxygen graphviz on Debian/Ubuntu, etc.). If they aren't
# present at configure time, this file silently disables the targets and
# prints a STATUS message — the rest of the build is unaffected.

find_program(AURORA_DOXYGEN NAMES doxygen)
find_program(AURORA_DOT     NAMES dot)

if(NOT AURORA_DOXYGEN)
    message(STATUS "doxygen not found; aurora-docs* targets disabled (brew/apt install doxygen)")
    return()
endif()

if(NOT AURORA_DOT)
    message(STATUS "graphviz 'dot' not found; aurora-docs* will still build but without diagrams")
endif()

set(_aurora_engine_doxyfile  "${CMAKE_SOURCE_DIR}/engine/Doxyfile")
set(_aurora_desktop_doxyfile "${CMAKE_SOURCE_DIR}/desktop/Doxyfile")

# --- engine ----------------------------------------------------------------
add_custom_target(
    aurora-docs-engine
    COMMAND "${AURORA_DOXYGEN}" "${_aurora_engine_doxyfile}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/engine"
    COMMENT "Generating engine API docs   →  engine/docs/html/index.html"
    VERBATIM
    USES_TERMINAL
)

# --- desktop ---------------------------------------------------------------
add_custom_target(
    aurora-docs-desktop
    COMMAND "${AURORA_DOXYGEN}" "${_aurora_desktop_doxyfile}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/desktop"
    COMMENT "Generating desktop API docs  →  desktop/docs/html/index.html"
    VERBATIM
    USES_TERMINAL
)

# --- meta ------------------------------------------------------------------
add_custom_target(
    aurora-docs
    DEPENDS aurora-docs-engine aurora-docs-desktop
    COMMENT "Generating Aurora Mail Doxygen docs (engine + desktop)"
)
