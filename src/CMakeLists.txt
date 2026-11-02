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
  motif/render/terminal.cpp
  motif/render/pager.cpp
  motif/render/notation.cpp
  motif/render/status_view.cpp
  motif/render/diff_view.cpp
  motif/render/log_view.cpp
  motif/git/subprocess.cpp
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

target_compile_options(motif_core PRIVATE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
)

add_executable(motif main.cpp)
target_link_libraries(motif PRIVATE motif_core)
