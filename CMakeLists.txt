cmake_minimum_required(VERSION 3.24)

project(motif
        VERSION 0.1.0
        DESCRIPTION "A music-native git wrapper for MuseScore repositories"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

# Single-config generators default to an unoptimised, unspecified build; pick one.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# fmt needs MSVC to read and write sources as UTF-8; so do motif's own glyphs.
if(MSVC)
  add_compile_options(/utf-8)
endif()

option(MOTIF_BUILD_TESTS "Build the motif test suite" ON)
# Every program lands in one folder: motif finds stave, and stave its verovio-data/,
# next to itself.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(MOTIF_WITH_STAVE "Build stave, the notation pager (pulls in Verovio and lunasvg)" ON)

# ---------------------------------------------------------------------------
# Third-party dependencies, vendored under external/ and compiled from source.
# No system packages and no network are needed to build the motif binary.
#
# pugixml and miniz ship amalgamated sources, so we compile those translation
# units directly rather than configuring their upstream CMake projects. Each
# library is also exposed under a namespaced alias (fmt::fmt, pugixml::pugixml,
# miniz::miniz) so the src/ and tests/ targets link against stable names.
# ---------------------------------------------------------------------------

# miniz -- ZIP container reading (.mscz is a zip archive)
add_library(miniz STATIC external/miniz-amalgamated/miniz.c)
target_include_directories(miniz SYSTEM PUBLIC external/miniz-amalgamated)
set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(miniz::miniz ALIAS miniz)

# pugixml -- XML parsing
add_library(pugixml STATIC external/pugixml/src/pugixml.cpp)
target_include_directories(pugixml SYSTEM PUBLIC external/pugixml/src)
set_target_properties(pugixml PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(pugixml::pugixml ALIAS pugixml)

# fmt -- string formatting (Apple clang 14 has no <format>)
add_library(fmt STATIC external/fmt/src/format.cc external/fmt/src/os.cc)
target_include_directories(fmt SYSTEM PUBLIC external/fmt/include)
set_target_properties(fmt PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(fmt::fmt ALIAS fmt)

# stave -- renders scores as engraved notation in the terminal (sixel, kitty or braille).
# A git submodule with nested submodules of its own (Verovio, lunasvg), so it must be
# initialised recursively. It defines the targets stave_core (library) and stave (program).
if(MOTIF_WITH_STAVE)
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/stave/external/verovio/include/vrv/toolkit.h)
    message(FATAL_ERROR
      "external/stave or its submodules are missing. Run:\n"
      "  git submodule update --init --recursive\n"
      "or configure with -DMOTIF_WITH_STAVE=OFF to build without it.")
  endif()
  add_subdirectory(external/stave)
endif()

add_subdirectory(src)

if(MOTIF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
