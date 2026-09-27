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
)
target_include_directories(edgeaudio PUBLIC include)

enable_testing()

function(ea_add_test name)
  add_executable(${name} tests/${name}.c)
  target_link_libraries(${name} edgeaudio ${ARGN})
  target_include_directories(${name} PRIVATE tests)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ea_add_test(test_smoke)
ea_add_test(test_flags)
ea_add_test(test_config)

# T-13 apparatus: adapter-boundary / purity checker runs as a test from W0 on.
add_test(NAME check_boundary
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_boundary.sh
                 ${CMAKE_CURRENT_SOURCE_DIR})
