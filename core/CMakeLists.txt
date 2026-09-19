# SPDX-License-Identifier: GPL-3.0-or-later
# Configurable standalone so CI can build and test the core without Qt, OpenCV
# or any of the vendored model runtimes.
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(openncomm_core LANGUAGES C)
  set(CMAKE_C_STANDARD 11)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  add_compile_options(-Wall -Wextra -Wshadow)
  enable_testing()
endif()

# The core is deliberately pure C with no I/O and no dependencies beyond libm.
# Everything patient-facing lives here so it can be tested without a camera, a
# microphone, a model or a window.
add_library(oc_core STATIC
  src/blink.c
  src/select.c
  src/morse.c
  src/gaze.c
  src/head.c
  src/ear.c
  src/answers.c
  src/calibrate.c
  src/blinkcal.c
)
target_include_directories(oc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(oc_core PUBLIC m)

foreach(suite blink morse gaze head answers calibrate blinkcal)
  add_executable(test_${suite} tests/test_${suite}.c)
  target_link_libraries(test_${suite} PRIVATE oc_core)
  target_include_directories(test_${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  add_test(NAME ${suite} COMMAND test_${suite})
endforeach()
