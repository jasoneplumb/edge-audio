# edge-audio host build (IMPLEMENTATION_PLAN.md W0). C11, warnings as errors.
cmake_minimum_required(VERSION 3.21)
project(edge_audio C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)

add_library(edgeaudio STATIC
  src/version.c
  src/flags.c
  src/config.c
  src/stream.c
  src/queue.c
  src/events.c
  src/history.c
  src/port_posix.c
)
find_package(Threads REQUIRED)
target_link_libraries(edgeaudio PRIVATE Threads::Threads m)
target_include_directories(edgeaudio PUBLIC include)

enable_testing()

add_library(ea_replay_lib STATIC tools/replay/replay_lib.c)
target_include_directories(ea_replay_lib PUBLIC tools/replay)

add_executable(replay tools/replay/replay.c)
target_link_libraries(replay ea_replay_lib)

add_executable(soak_host tools/soak_host.c)
target_link_libraries(soak_host edgeaudio Threads::Threads)

add_library(ea_testsupport STATIC tests/support/sim_adapter.c
                                  tests/support/recorder.c)
target_link_libraries(ea_testsupport PUBLIC edgeaudio)
target_include_directories(ea_testsupport PUBLIC tests)

function(ea_add_test name)
  add_executable(${name} tests/${name}.c)
  target_link_libraries(${name} ea_testsupport ${ARGN})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ea_add_test(test_smoke)
ea_add_test(test_flags)
ea_add_test(test_config)
ea_add_test(test_capture)
ea_add_test(test_queue)
ea_add_test(test_lifecycle)
ea_add_test(test_events)
ea_add_test(test_stream_info)
ea_add_test(test_history)
ea_add_test(test_replay ea_replay_lib)
ea_add_test(test_concurrency Threads::Threads)

ea_add_test(test_boundary)
ea_add_test(test_recorded)

add_library(ea_sg_profile STATIC adapters/soundguardian/profile.c)
target_link_libraries(ea_sg_profile PUBLIC edgeaudio)
ea_add_test(test_profile ea_sg_profile)

# W5 stand-in gate (plan §6): the target adapter sources must at least
# compile against stub headers on the host (syntax + boundary only; covers
# nothing about placement or timing — K-02/T-17 stay verification pending).
add_test(NAME target_stub_compile
         COMMAND ${CMAKE_C_COMPILER} -fsyntax-only -std=c11 -Wall -Werror
                 -DEDGE_AUDIO_TARGET_STUBS=1
                 -I${CMAKE_CURRENT_SOURCE_DIR}/include
                 -I${CMAKE_CURRENT_SOURCE_DIR}/adapters/soundguardian
                 ${CMAKE_CURRENT_SOURCE_DIR}/adapters/soundguardian/es8311_i2s.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/adapters/soundguardian/port_esp.c)

# T-13 apparatus: adapter-boundary / purity checker runs as a test from W0 on
# (wrapped by tests/test_boundary.c so the citation lives in the test tree).
add_compile_definitions(EA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
