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

option(MOTIF_BUILD_TESTS "Build the motif test suite" ON)

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

add_subdirectory(src)

if(MOTIF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
