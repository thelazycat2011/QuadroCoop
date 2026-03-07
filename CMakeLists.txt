# ================================================================
#  QuadroCoop — CMakeLists.txt
#  Geode SDK mod build configuration
#
#  Requirements:
#    - CMake 3.21+
#    - GEODE_SDK environment variable pointing to your Geode SDK clone
#    - C++20 compiler (MSVC 2019+, Clang 13+, or GCC 11+)
#
#  Usage:
#    cmake -B build
#    cmake --build build --config RelWithDebInfo
#
#  The create_geode_file() call packages the compiled .dll/.dylib/.so
#  together with mod.json, resources/, and about.md into a .geode
#  archive ready for installation or submission to the index.
# ================================================================

cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)  # prevents symbol leaks across mods

project(QuadroCoop VERSION 2.0.0)

# ── Locate Geode SDK ─────────────────────────────────────────────
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR
        "GEODE_SDK environment variable is not set.\n"
        "Set it to the root of your Geode SDK clone, e.g.:\n"
        "  export GEODE_SDK=/path/to/geode   (Linux / macOS)\n"
        "  set GEODE_SDK=C:\\path\\to\\geode  (Windows)\n"
    )
else()
    message(STATUS "Geode SDK: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# ── Mod target ───────────────────────────────────────────────────
add_library(${PROJECT_NAME} SHARED
    src/main.cpp
)

# Add additional source files here as the project grows, e.g.:
#   src/TriggerSystem.cpp
#   src/SettingsPopup.cpp

target_include_directories(${PROJECT_NAME} PRIVATE
    src/
)

# Strict warnings help catch Geode-specific pitfalls early.
if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

target_link_libraries(${PROJECT_NAME} geode-sdk)

# ── Package into .geode archive ──────────────────────────────────
# Reads mod.json, bundles resources/ and about.md automatically.
create_geode_file(${PROJECT_NAME})
