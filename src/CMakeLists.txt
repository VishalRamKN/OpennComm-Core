# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

# One ggml, shared by both runtimes. scripts/fetch-deps.sh builds llama.cpp's
# copy, installs it into third_party/prefix, and builds whisper.cpp against it;
# the reasoning is in that script. The headers must come from that prefix too:
# whisper.cpp's own ggml/include is still in the source tree at the older
# version, and compiling against 0.23 headers while linking 0.24 is precisely
# the mismatch the shared build exists to remove.
set(GGML_PREFIX ${CMAKE_SOURCE_DIR}/third_party/prefix)
if(NOT EXISTS ${GGML_PREFIX}/lib/libggml.so)
  message(FATAL_ERROR "shared ggml missing - run scripts/fetch-deps.sh")
endif()
add_library(ggml_c SHARED IMPORTED GLOBAL)
set_target_properties(ggml_c PROPERTIES
  IMPORTED_LOCATION ${GGML_PREFIX}/lib/libggml.so
  INTERFACE_INCLUDE_DIRECTORIES ${GGML_PREFIX}/include)

# llama.cpp is built by scripts/fetch-deps.sh into third_party/llama.cpp/build.
set(LLAMA_DIR ${CMAKE_SOURCE_DIR}/third_party/llama.cpp)
set(LLAMA_LIB ${LLAMA_DIR}/build/bin/libllama.so)
if(NOT EXISTS ${LLAMA_LIB})
  message(FATAL_ERROR "libllama.so missing - run scripts/fetch-deps.sh")
endif()
add_library(llama_c SHARED IMPORTED GLOBAL)
set_target_properties(llama_c PROPERTIES
  IMPORTED_LOCATION ${LLAMA_LIB}
  INTERFACE_INCLUDE_DIRECTORIES ${LLAMA_DIR}/include
  INTERFACE_LINK_LIBRARIES ggml_c)

set(WHISPER_DIR ${CMAKE_SOURCE_DIR}/third_party/whisper.cpp)
set(WHISPER_LIB ${WHISPER_DIR}/build/bin/libwhisper.so)
if(NOT EXISTS ${WHISPER_LIB})
  message(FATAL_ERROR "libwhisper.so missing - run scripts/fetch-deps.sh")
endif()
add_library(whisper_c SHARED IMPORTED GLOBAL)
set_target_properties(whisper_c PROPERTIES
  IMPORTED_LOCATION ${WHISPER_LIB}
  INTERFACE_INCLUDE_DIRECTORIES ${WHISPER_DIR}/include
  INTERFACE_LINK_LIBRARIES ggml_c)

find_package(OpenCV REQUIRED COMPONENTS core videoio imgproc)

# sqlite3 directly rather than Qt6::Sql: one public-domain C library that is
# already on every Linux, against a Qt driver plugin that would have to be
# located and bundled. See src/history.h.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

qt_standard_project_setup()

# Everything but main() lives in a library, so development tools (the offscreen
# screenshot grabber in spike/) can drive the real window rather than a mock-up.
qt_add_library(openncomm_lib STATIC
  mainwindow.cc
  camera.cc
  speech.cc
  generator.cc
  listener.cc
  caliboverlay.cc
  paths.cc
  optionbutton.cc
  profile.cc
  blinkcaloverlay.cc
  history.cc
  historydialog.cc
  morseview.cc
  nosepointer.cc
)
target_link_libraries(openncomm_lib PUBLIC
  oc_core
  mediapipe_c
  llama_c
  whisper_c
  Qt6::Widgets
  PkgConfig::SQLITE3
  ${OpenCV_LIBS}
)
target_include_directories(openncomm_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${OpenCV_INCLUDE_DIRS}
)
set_target_properties(openncomm_lib PROPERTIES AUTOMOC ON)

qt_add_executable(openncomm main.cc)
target_link_libraries(openncomm PRIVATE openncomm_lib)
# Qt is LGPL-3.0: it must stay dynamically linked. See THIRD_PARTY.md.
set_target_properties(openncomm PROPERTIES
  AUTOMOC ON
  INSTALL_RPATH "$ORIGIN/../lib"
)

# History is the one part outside core/ with logic worth pinning down: what
# counts as an exchange, what a search matches, and above all that clearing
# actually removes the words from the file. It needs no camera, model or
# display, so it runs with the rest of the suites locally. CI builds core/
# standalone and does not reach it.
add_executable(test_history tests/test_history.cc history.cc)
target_link_libraries(test_history PRIVATE Qt6::Core PkgConfig::SQLITE3)
target_include_directories(test_history PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/core/tests)
add_test(NAME history COMMAND test_history)
