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

# ---- Installation -----------------------------------------------------------
include(GNUInstallDirs)

# A private library directory. Nothing vendored here -- mediapipe, llama.cpp,
# whisper.cpp, ggml, piper -- is packaged by any distribution, so it travels
# with the application rather than pretending to be a system library. Qt,
# OpenCV and sqlite3 are the opposite case and come from the distribution.
set(OPENNCOMM_PKGLIBDIR ${CMAKE_INSTALL_LIBDIR}/openncomm)

# paths.cc needs to know how this distribution spells libdir so it can find
# piper relative to the executable. See the comment there.
target_compile_definitions(openncomm_lib PRIVATE
  OPENNCOMM_LIBDIR="${CMAKE_INSTALL_LIBDIR}")

# Qt is LGPL-3.0: it must stay dynamically linked. See THIRD_PARTY.md.
set_target_properties(openncomm PROPERTIES
  AUTOMOC ON
  INSTALL_RPATH "$ORIGIN/../${OPENNCOMM_PKGLIBDIR}"
)

# The version reaches the binary from the VERSION file, via project(). Without
# this the program cannot say which build it is, and a report of a wrongly
# spoken answer cannot be tied to one.
target_compile_definitions(openncomm PRIVATE OPENNCOMM_VERSION="${PROJECT_VERSION}")

install(TARGETS openncomm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# install(DIRECTORY) rather than install(FILES), because these are symlink
# chains -- libllama.so -> .so.0 -> .so.0.4.1 -- and the loader asks for the
# SONAME in the middle. install(FILES) would dereference each link and install
# three identical copies under the wrong names.
foreach(libdir ${GGML_PREFIX}/lib ${LLAMA_DIR}/build/bin ${WHISPER_DIR}/build/bin
                ${CMAKE_SOURCE_DIR}/third_party/mediapipe/lib)
  install(DIRECTORY ${libdir}/
          DESTINATION ${OPENNCOMM_PKGLIBDIR}
          FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                           GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
          FILES_MATCHING
            PATTERN "libggml*.so*"
            PATTERN "libllama.so*"
            PATTERN "libwhisper.so*"
            PATTERN "libmediapipe.so*"
          # FILES_MATCHING still creates every subdirectory it walks, empty,
          # so ggml's CMake package directory has to be excluded by name.
          PATTERN "cmake" EXCLUDE
          PATTERN "pkgconfig" EXCLUDE)
endforeach()

# Piper ships as a prebuilt tree with its own onnxruntime, espeak-ng and
# phoneme data, linked with RUNPATH $ORIGIN. It is copied whole and needs no
# patching; USE_SOURCE_PERMISSIONS keeps the executable bits it arrived with.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/piper/
        DESTINATION ${OPENNCOMM_PKGLIBDIR}/piper
        USE_SOURCE_PERMISSIONS)

# The only model small enough to ship, and the only one without which there is
# no input at all. The rest -- about 1.2 GB -- are downloaded into the user's
# XDG data directory on first use; see src/paths.h.
install(FILES ${CMAKE_SOURCE_DIR}/models/face_landmarker.task
        DESTINATION ${CMAKE_INSTALL_DATADIR}/openncomm/models)

# The vendored libraries carry RUNPATHs that are wrong everywhere but the
# machine that built them. cmake/patch-rpath.cmake explains each one.
# Deliberately not a configure-time error. Running from the source tree with
# ./run.sh needs no patching and should not need patchelf installed; only
# `cmake --install` does, and patch-rpath.cmake fails there with the same
# advice if it is missing.
find_program(PATCHELF_EXECUTABLE patchelf)
install(CODE "set(OC_PATCHELF \"${PATCHELF_EXECUTABLE}\")")
# The relative path only. CMAKE_INSTALL_PREFIX is resolved inside the script,
# at install time, because `cmake --install --prefix` overrides it after this
# file has already been expanded.
install(CODE "set(OC_PKGLIBDIR_REL \"${OPENNCOMM_PKGLIBDIR}\")")
install(SCRIPT ${CMAKE_SOURCE_DIR}/cmake/patch-rpath.cmake)

# The desktop entry, icons and AppStream metadata are all named for the
# AppStream component id, not for the binary. The id has to be reverse-DNS and
# has to match the .desktop filename, or GNOME Software and KDE Discover will
# not associate the metadata with the application. The executable and the
# package are still plain "openncomm".
set(OPENNCOMM_APPID "io.github.vishalramkn.OpennComm-Core")

install(FILES ${CMAKE_SOURCE_DIR}/packaging/${OPENNCOMM_APPID}.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_SOURCE_DIR}/packaging/${OPENNCOMM_APPID}.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
install(FILES ${CMAKE_SOURCE_DIR}/packaging/${OPENNCOMM_APPID}.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${CMAKE_SOURCE_DIR}/packaging/${OPENNCOMM_APPID}.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

# DISCLAIMER is not paperwork here: this is not a medical device, and someone
# who installs a package rather than reading the repository should still be
# able to find that out on their own machine.
install(FILES ${CMAKE_SOURCE_DIR}/README.md
              ${CMAKE_SOURCE_DIR}/DISCLAIMER
              ${CMAKE_SOURCE_DIR}/THIRD_PARTY.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/openncomm)

# The licence of everything the package redistributes, verbatim, beside the
# binaries it applies to. A summary in THIRD_PARTY.md is not what Apache-2.0
# section 4 or the MIT notice clause ask for, and espeak-ng is GPL-3.0, which
# additionally wants the source offer that WRITTEN-OFFER carries.
install(FILES ${CMAKE_SOURCE_DIR}/packaging/WRITTEN-OFFER
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES
          ${CMAKE_SOURCE_DIR}/third_party/licenses/mediapipe.LICENSE
          ${CMAKE_SOURCE_DIR}/third_party/licenses/piper.LICENSE
          ${CMAKE_SOURCE_DIR}/third_party/licenses/espeak-ng.COPYING
          ${CMAKE_SOURCE_DIR}/third_party/licenses/onnxruntime.LICENSE
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses)
# These two are checked out as source, so their licences come from the trees
# themselves rather than being fetched separately.
install(FILES ${LLAMA_DIR}/LICENSE
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME llama.cpp.LICENSE)
install(FILES ${WHISPER_DIR}/LICENSE
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME whisper.cpp.LICENSE)

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
