add_library(motif_core STATIC
  motif/score/model.cpp
  motif/score/pitch.cpp
  motif/score/ticks.cpp
  motif/mscz/archive.cpp
  motif/mscz/parser.cpp
  motif/diff/align.cpp
  motif/diff/note_diff.cpp
  motif/diff/engine.cpp
  motif/analysis/transform.cpp
  motif/export/mei.cpp
  motif/render/terminal.cpp
  motif/render/pager.cpp
  motif/render/notation.cpp
  motif/render/status_view.cpp
  motif/render/diff_view.cpp
  motif/render/log_view.cpp
  $<IF:$<PLATFORM_ID:Windows>,motif/git/subprocess_windows.cpp,motif/git/subprocess_posix.cpp>
  motif/git/repo.cpp
  motif/cli/dispatch.cpp
  motif/cli/options.cpp
  motif/cli/score_source.cpp
  motif/cli/cmd_status.cpp
  motif/cli/cmd_diff.cpp
  motif/cli/cmd_log.cpp
  motif/cli/cmd_inspect.cpp
)

target_include_directories(motif_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(motif_core
  PUBLIC  fmt::fmt
  PRIVATE pugixml::pugixml miniz::miniz
)

if(MSVC)
  target_compile_options(motif_core PRIVATE /W4 /permissive-)
  target_compile_definitions(motif_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(motif_core PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
  )
endif()

add_executable(motif main.cpp)
# On Windows, run with UTF-8 as the process code page, so argv, file names and paths
# from git all arrive as the UTF-8 the rest of motif expects.
if(WIN32)
  target_sources(motif PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/utf8.manifest)
endif()
target_link_libraries(motif PRIVATE motif_core)

# `motif diff` shows scores through stave, run as a program of its own, so building
# motif builds stave too.
if(TARGET stave)
  add_dependencies(motif stave)
  target_compile_definitions(motif_core PRIVATE MOTIF_HAVE_STAVE=1)
endif()
