include(FetchContent)

FetchContent_Declare(
  Catch2
  URL      https://github.com/catchorg/Catch2/archive/refs/tags/v3.8.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Catch2)

add_executable(motif_tests
  test_subprocess.cpp
  test_pitch.cpp
  test_ticks.cpp
  test_parser.cpp
  test_align.cpp
  test_diff.cpp
  test_transform.cpp
  test_terminal.cpp
  test_options.cpp
)

target_link_libraries(motif_tests PRIVATE motif_core Catch2::Catch2WithMain)

# The sample concerto doubles as a regression fixture for the parser. It is optional:
# the test skips itself when the file is absent.
set(MOTIF_SAMPLE_SCORE "${CMAKE_SOURCE_DIR}/tests/Piano Concerto in G Minor.mscz")
if(EXISTS "${MOTIF_SAMPLE_SCORE}")
  target_compile_definitions(motif_tests PRIVATE MOTIF_SAMPLE_SCORE="${MOTIF_SAMPLE_SCORE}")
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(motif_tests)

# End-to-end: runs the real binary against a real git repository and compares its
# output with tests/e2e/expected. Regenerate those files with:
#   tests/e2e/run_e2e.sh build/src/motif --update
add_test(NAME e2e COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/e2e/run_e2e.sh $<TARGET_FILE:motif>)
