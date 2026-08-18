cmake_minimum_required(VERSION 3.24)

project(motif
        VERSION 0.1.0
        DESCRIPTION "Git Wrapper For Musescore .mscz Files"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

# miniz library
add_library(miniz STATIC external/miniz-amalgamated/miniz.c)
target_include_directories(miniz SYSTEM PUBLIC external/miniz-amalgamated)

# pugixml library
add_library(pugixml STATIC external/pugixml/src/pugixml.cpp)
target_include_directories(pugixml SYSTEM PUBLIC external/pugixml/src)

# fmt library
add_library(fmt STATIC external/fmt/src/format.cc external/fmt/src/os.cc)
target_include_directories(fmt SYSTEM PUBLIC external/fmt/include)

add_executable(motif src/main.cpp
        src/cli/git.cpp
        src/cli/git.hpp
        src/commands/status.cpp
        src/commands/status.hpp
        src/cli/zipreader.cpp
        src/cli/zipreader.hpp)

target_link_libraries(motif PRIVATE miniz pugixml fmt)