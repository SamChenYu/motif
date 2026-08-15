cmake_minimum_required(VERSION 3.24)

project(motif
        VERSION 0.1.0
        DESCRIPTION "Git Wrapper For Musescore .mscz Files"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

add_executable(motif src/main.cpp
        src/cli/git.cpp
        src/cli/git.h
        src/commands/status.cpp
        src/commands/status.h)