# SPDX-License-Identifier: GPL-3.0-or-later
#
# The four vendored runtimes -- ggml, llama.cpp, whisper.cpp and MediaPipe --
# as imported CMake targets, for every platform this builds on.
#
# They are declared here rather than in src/ and spike/ because both need them
# and because the difference between platforms is entirely in where the files
# are and what they are called, not in what they are. Keeping that difference in
# one file is what stops a Windows path from being fixed in src/ and left wrong
# in spike/ -- which is a build that succeeds and then loads the wrong thing.
#
# None of these is built here. scripts/fetch-deps.sh (Unix) and
# scripts/fetch-deps.ps1 (Windows) produce them, pinned and checksummed, and the
# contract between those scripts and this file is the set of paths below.

set(OC_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(GGML_PREFIX ${OC_THIRD_PARTY}/prefix)
set(LLAMA_DIR ${OC_THIRD_PARTY}/llama.cpp)
set(WHISPER_DIR ${OC_THIRD_PARTY}/whisper.cpp)
set(MP_DIR ${OC_THIRD_PARTY}/mediapipe)

if(WIN32)
  # One flat pair of directories, populated by scripts/fetch-deps.ps1.
  #
  # The alternative -- reading the artifacts out of each project's own build
  # tree, the way the Unix side does -- does not survive contact with MSVC: the
  # .dll and the .lib for one library land in different directories, and which
  # directories depends on the generator and on whether the build is
  # multi-config. The fetch script knows where its own build put them, so it
  # normalises there and this file gets to be simple.
  set(OC_WIN_BIN ${OC_THIRD_PARTY}/win/bin)
  set(OC_WIN_LIB ${OC_THIRD_PARTY}/win/lib)
endif()

# oc_import <target> <basename> <include dirs...>
#
# Declares one imported shared library. On Unix that is a single .so; on Windows
# it is a .dll to load and a .lib to link against, which is what IMPORTED_IMPLIB
# is for. Missing files are a configure-time error naming the script that
# produces them, because the alternative is a link error listing symbols.
function(oc_import target basename)
  if(WIN32)
    set(_dll ${OC_WIN_BIN}/${basename}.dll)
    set(_lib ${OC_WIN_LIB}/${basename}.lib)
    foreach(_f ${_dll} ${_lib})
      if(NOT EXISTS ${_f})
        message(FATAL_ERROR
          "${_f} is missing.\n"
          "Run scripts/fetch-deps.ps1 from a Visual Studio developer prompt.")
      endif()
    endforeach()
    add_library(${target} SHARED IMPORTED GLOBAL)
    set_target_properties(${target} PROPERTIES
      IMPORTED_LOCATION ${_dll}
      IMPORTED_IMPLIB ${_lib})
  else()
    set(_so ${ARGV2})
    if(NOT EXISTS ${_so})
      message(FATAL_ERROR "${_so} is missing - run scripts/fetch-deps.sh")
    endif()
    add_library(${target} SHARED IMPORTED GLOBAL)
    set_target_properties(${target} PROPERTIES IMPORTED_LOCATION ${_so})
  endif()
endfunction()

# One ggml, shared by both runtimes. scripts/fetch-deps.sh builds llama.cpp's
# copy, installs it into third_party/prefix, and builds whisper.cpp against it;
# the reasoning is in that script. The headers must come from that prefix too:
# whisper.cpp's own ggml/include is still in the source tree at the older
# version, and compiling against 0.23 headers while linking 0.24 is precisely
# the mismatch the shared build exists to remove.
oc_import(ggml_c ggml ${GGML_PREFIX}/lib/libggml.so)
set_target_properties(ggml_c PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES ${GGML_PREFIX}/include)

oc_import(llama_c llama ${LLAMA_DIR}/build/bin/libllama.so)
set_target_properties(llama_c PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES ${LLAMA_DIR}/include
  INTERFACE_LINK_LIBRARIES ggml_c)

oc_import(whisper_c whisper ${WHISPER_DIR}/build/bin/libwhisper.so)
set_target_properties(whisper_c PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES ${WHISPER_DIR}/include
  INTERFACE_LINK_LIBRARIES ggml_c)

# MediaPipe arrives as a prebuilt library inside a Python wheel -- there is no
# source build of the C API to do. The wheel carries no import library on
# Windows, so fetch-deps.ps1 generates one from the DLL's export table; see the
# note there.
oc_import(mediapipe_c libmediapipe ${MP_DIR}/lib/libmediapipe.so)
set_target_properties(mediapipe_c PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES ${MP_DIR}/include)
