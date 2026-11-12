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

# Linux ALSA source adapter (adapters/alsa; issue #5). Built and tested when
# alsa-lib is present; EA_REQUIRE_ALSA=ON (CI) turns its absence into an error
# so the adapter cannot silently drop out of the gate.
option(EA_REQUIRE_ALSA "Fail configuration if alsa-lib is not found" OFF)
find_package(ALSA)
if(ALSA_FOUND)
  add_library(ea_alsa STATIC adapters/alsa/alsa.c)
  target_link_libraries(ea_alsa PUBLIC edgeaudio PRIVATE ALSA::ALSA)
  target_include_directories(ea_alsa PUBLIC adapters/alsa)
  ea_add_test(test_alsa ea_alsa)
elseif(EA_REQUIRE_ALSA)
  message(FATAL_ERROR "EA_REQUIRE_ALSA=ON but alsa-lib was not found")
endif()

# WAV file source adapter (adapters/wav; issue #8): real-time paced file
# playback with fault injection. POSIX only, so built and tested everywhere.
add_library(ea_wav STATIC adapters/wav/wav.c)
target_link_libraries(ea_wav PUBLIC edgeaudio)
target_include_directories(ea_wav PUBLIC adapters/wav)
ea_add_test(test_wav ea_wav)

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

# Public-release invariants (a repository convention, not spec ids): nothing
# restricted tracked, license files present, consistent version strings with no
# suffixed form of the release version, documentation references that resolve,
# and cited commit hashes that are ancestors of HEAD. Exit code 77 = skipped
# (no Git work tree, or a shallow clone for the cited-commit check).
add_test(NAME release_invariants
         COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_release.sh
                 ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME release_cited_commits
         COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_cited_commits.sh
                 ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME release_checks_selftest
         COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/release_checks_selftest.sh
                 ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(release_cited_commits release_checks_selftest
                     PROPERTIES SKIP_RETURN_CODE 77)

# T-13 apparatus: adapter-boundary / purity checker runs as a test from W0 on
# (wrapped by tests/test_boundary.c so the citation lives in the test tree).
add_compile_definitions(EA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
