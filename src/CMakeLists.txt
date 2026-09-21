# SPDX-License-Identifier: GPL-3.0-or-later

# Multimedia only on Windows, and only for audio.cc.
#
# On Linux sound goes through ffmpeg and paplay, which are already installed and
# already know whether the machine is running PulseAudio or PipeWire. Windows
# has neither of those programs and one audio API that is always present, which
# Qt speaks directly. Asking for Multimedia on Linux too would add a dependency
# to every distribution package to serve code that platform never compiles.
set(OC_QT_COMPONENTS Widgets)
if(WIN32)
  list(APPEND OC_QT_COMPONENTS Multimedia)
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS ${OC_QT_COMPONENTS})

find_package(OpenCV REQUIRED COMPONENTS core videoio imgproc)

# sqlite3 directly rather than Qt6::Sql: one public-domain C library that is
# already on every Linux, against a Qt driver plugin that would have to be
# located and bundled. See src/history.h.
#
# Found two ways because it arrives two ways. On Linux it is a system library
# with a .pc file; on Windows it comes from vcpkg, which ships no pkg-config and
# is found through CMake's own FindSQLite3 module instead.
if(WIN32)
  find_package(SQLite3 REQUIRED)
  set(OC_SQLITE SQLite::SQLite3)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
  set(OC_SQLITE PkgConfig::SQLITE3)
endif()

qt_standard_project_setup()

# Everything but main() lives in a library, so development tools (the offscreen
# screenshot grabber in spike/) can drive the real window rather than a mock-up.
qt_add_library(openncomm_lib STATIC
  mainwindow.cc
  camera.cc
  speech.cc
  generator.cc
  listener.cc
  audio.cc
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
  ${OC_SQLITE}
  ${OpenCV_LIBS}
)
if(WIN32)
  target_link_libraries(openncomm_lib PUBLIC Qt6::Multimedia)
endif()
target_include_directories(openncomm_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${OpenCV_INCLUDE_DIRS}
)
set_target_properties(openncomm_lib PROPERTIES AUTOMOC ON)

qt_add_executable(openncomm main.cc)
target_link_libraries(openncomm PRIVATE openncomm_lib)

# WIN32_EXECUTABLE keeps a console window from opening behind the application.
# It is not cosmetic: this is a full-screen program used by somebody who cannot
# operate a mouse or a keyboard, and a stray console stealing focus is a stuck
# session for them. Diagnostics still reach a console when openncomm.exe is
# started from one, because Qt attaches to the parent's.
if(WIN32)
  set_target_properties(openncomm PROPERTIES WIN32_EXECUTABLE ON)
endif()

# ---- Installation -----------------------------------------------------------
include(GNUInstallDirs)

# Where each kind of file goes. Unix has a prefix with a shape that is not
# negotiable; Windows has one directory, because that is what survives being
# copied to a USB stick and because Program Files is read-only to the patient
# who will be using this. src/paths.cc searches for both layouts.
if(WIN32)
  set(OPENNCOMM_PKGLIBDIR .)
  set(OPENNCOMM_MODELDIR models)
  set(OPENNCOMM_PIPERDIR piper)
  set(OPENNCOMM_DOCDIR doc)
else()
  # A private library directory. Nothing vendored here -- mediapipe, llama.cpp,
  # whisper.cpp, ggml, piper -- is packaged by any distribution, so it travels
  # with the application rather than pretending to be a system library. Qt,
  # OpenCV and sqlite3 are the opposite case and come from the distribution.
  set(OPENNCOMM_PKGLIBDIR ${CMAKE_INSTALL_LIBDIR}/openncomm)
  set(OPENNCOMM_MODELDIR ${CMAKE_INSTALL_DATADIR}/openncomm/models)
  set(OPENNCOMM_PIPERDIR ${OPENNCOMM_PKGLIBDIR}/piper)
  set(OPENNCOMM_DOCDIR ${CMAKE_INSTALL_DOCDIR})
endif()

# paths.cc needs to know how this distribution spells libdir so it can find
# piper relative to the executable. See the comment there.
target_compile_definitions(openncomm_lib PRIVATE
  OPENNCOMM_LIBDIR="${CMAKE_INSTALL_LIBDIR}")

# Qt is LGPL-3.0: it must stay dynamically linked. See THIRD_PARTY.md.
set_target_properties(openncomm PROPERTIES AUTOMOC ON)
if(NOT WIN32)
  # Windows resolves DLLs from the directory the .exe was loaded from, which is
  # where they are installed, so there is no equivalent of this to set.
  set_target_properties(openncomm PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${OPENNCOMM_PKGLIBDIR}")
endif()

# The version reaches the binary from the VERSION file, via project(). Without
# this the program cannot say which build it is, and a report of a wrongly
# spoken answer cannot be tied to one.
target_compile_definitions(openncomm PRIVATE OPENNCOMM_VERSION="${PROJECT_VERSION}")

install(TARGETS openncomm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(WIN32)
  # TARGET_RUNTIME_DLLS below arrived in CMake 3.21, and the project floor is
  # 3.20 because Linux does not use it. Checked here rather than with a second
  # cmake_minimum_required() at the top of the tree: WIN32 is not set until
  # project() has run, so a guard up there is never true and quietly does
  # nothing -- which is how this was written the first time.
  if(CMAKE_VERSION VERSION_LESS 3.21)
    message(FATAL_ERROR
      "Building for Windows needs CMake 3.21 or newer for TARGET_RUNTIME_DLLS; "
      "this is ${CMAKE_VERSION}.")
  endif()

  # The vendored runtimes, normalised into one directory by
  # scripts/fetch-deps.ps1. No symlink chains and no SONAMEs on Windows: a DLL
  # is one file with one name.
  install(DIRECTORY ${OC_WIN_BIN}/
          DESTINATION ${OPENNCOMM_PKGLIBDIR}
          FILES_MATCHING PATTERN "*.dll")

  # Everything else the executable loads that is not ours -- OpenCV, and the Qt
  # libraries themselves. CMake reads this off the imported targets that were
  # actually linked, so a dependency added later cannot be forgotten here the
  # way a hand-written list would forget it.
  install(FILES $<TARGET_RUNTIME_DLLS:openncomm> DESTINATION ${CMAKE_INSTALL_BINDIR})

  # sqlite3 is the exception, and it is a quiet one. CMake's own FindSQLite3
  # declares SQLite::SQLite3 as an UNKNOWN imported library pointing at the
  # import library, and TARGET_RUNTIME_DLLS follows only SHARED ones -- so the
  # DLL beside it is silently left out. The application then installs, starts,
  # and dies when it first opens the conversation history.
  #
  # Nothing is installed when no DLL is found, because that is what a static
  # sqlite looks like and it needs none.
  get_filename_component(_sqlite_root "${SQLite3_INCLUDE_DIR}" DIRECTORY)
  find_file(OC_SQLITE_DLL
    NAMES sqlite3.dll
    HINTS "${_sqlite_root}/bin" "${_sqlite_root}/lib"
    NO_DEFAULT_PATH)
  if(OC_SQLITE_DLL)
    install(FILES ${OC_SQLITE_DLL} DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()

  # Qt needs more than its DLLs: without the platform plugin the application
  # exits at startup with "could not find or load the Qt platform plugin
  # windows", which names a file that was never copied rather than anything the
  # person did. windeployqt is what knows the full list.
  #
  # No NO_TRANSLATIONS: that keyword only exists from Qt 6.5, and this still
  # declares 6.4 as its floor. Qt's translation catalogues are a few hundred
  # kilobytes next to 1.4 GB of models, which is not worth a version guard.
  qt_generate_deploy_app_script(
    TARGET openncomm
    OUTPUT_SCRIPT OC_DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR)
  install(SCRIPT ${OC_DEPLOY_SCRIPT})
else()
  # install(DIRECTORY) rather than install(FILES), because these are symlink
  # chains -- libllama.so -> .so.0 -> .so.0.4.1 -- and the loader asks for the
  # SONAME in the middle. install(FILES) would dereference each link and install
  # three identical copies under the wrong names.
  foreach(libdir ${GGML_PREFIX}/lib ${LLAMA_DIR}/build/bin ${WHISPER_DIR}/build/bin
                  ${MP_DIR}/lib)
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
endif()

# Piper ships as a prebuilt tree with its own onnxruntime, espeak-ng and
# phoneme data. On Linux it is linked with RUNPATH $ORIGIN and on Windows the
# loader searches the executable's own directory, so in both cases it is copied
# whole and needs no patching; USE_SOURCE_PERMISSIONS keeps the executable bits
# it arrived with.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/piper/
        DESTINATION ${OPENNCOMM_PIPERDIR}
        USE_SOURCE_PERMISSIONS)

# Every model, in the package. All of it -- roughly 1.3 GB against 200 MB of
# code -- and the reason is not convenience.
#
# This program is installed for somebody who cannot speak. A package that holds
# the face model and nothing else installs without complaint, opens, tracks the
# face, and then quietly has no voice, no speech recognition and no written
# answers, with nothing on screen saying which of those are missing rather than
# broken. The caregiver setting it up, often at a bedside, has to find out from
# documentation that a further 1.2 GB has to be downloaded before the thing
# they installed does what it said. That is a bad way to learn it.
#
# So the size is paid once, at download, by a person who is looking at a
# progress bar and knows what they are waiting for -- not later, by a patient
# who cannot say that something is wrong.
#
# Listed by name rather than globbed: a glob over models/ turns a file that
# the fetch script failed to fetch into a package that is silently incomplete,
# which is exactly the failure this block exists to end.
set(OPENNCOMM_MODEL_FILES
  face_landmarker.task                    # face tracking -- without it, no input at all
  ggml-base.en-q5_1.bin                   # speech recognition (whisper.cpp)
  qwen2.5-1.5b-instruct-q4_k_m.gguf       # answer generation (llama.cpp)
)
# One voice of each gender. Which one a patient is given is not cosmetic: it is
# the voice other people will hear as theirs. Both ship so the choice can be
# made in Settings rather than by another download. Each .onnx needs its .json
# beside it -- Piper reads the sample rate and the phoneme map from there, and
# a voice whose config is missing is a voice that cannot be loaded.
set(OPENNCOMM_VOICE_FILES
  voices/en_US-amy-medium.onnx
  voices/en_US-amy-medium.onnx.json
  voices/en_US-joe-medium.onnx
  voices/en_US-joe-medium.onnx.json
)
foreach(m ${OPENNCOMM_MODEL_FILES} ${OPENNCOMM_VOICE_FILES})
  if(NOT EXISTS ${CMAKE_SOURCE_DIR}/models/${m})
    message(FATAL_ERROR "models/${m} missing - run the fetch script for this platform")
  endif()
  # get_filename_component, not a second list: the voices live in a
  # subdirectory and must keep it, because src/speech.cc finds voices by
  # scanning <models>/voices for *.onnx.
  get_filename_component(_subdir ${m} DIRECTORY)
  install(FILES ${CMAKE_SOURCE_DIR}/models/${m}
          DESTINATION ${OPENNCOMM_MODELDIR}/${_subdir})
endforeach()

if(NOT WIN32)
  # The vendored libraries carry RUNPATHs that are wrong everywhere but the
  # machine that built them. cmake/patch-rpath.cmake explains each one. Windows
  # has no equivalent: a DLL records no search path at all.
  #
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
endif()

# The desktop entry, icons and AppStream metadata are all named for the
# AppStream component id, not for the binary. The id has to be reverse-DNS and
# has to match the .desktop filename, or GNOME Software and KDE Discover will
# not associate the metadata with the application. The executable and the
# package are still plain "openncomm".
set(OPENNCOMM_APPID "io.github.vishalramkn.OpennComm-Core")

if(WIN32)
  # None of the freedesktop metadata means anything here; the Start Menu entry
  # comes from the installer instead. The icon still ships, because the
  # installer needs a .ico and the .svg is what it is generated from.
  install(FILES ${CMAKE_SOURCE_DIR}/packaging/${OPENNCOMM_APPID}.png
          DESTINATION . RENAME openncomm.png)
else()
  install(FILES ${CMAKE_SOURCE_DIR}/packaging/${OPENNCOMM_APPID}.desktop
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES ${CMAKE_SOURCE_DIR}/packaging/${OPENNCOMM_APPID}.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
  install(FILES ${CMAKE_SOURCE_DIR}/packaging/${OPENNCOMM_APPID}.svg
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
  install(FILES ${CMAKE_SOURCE_DIR}/packaging/${OPENNCOMM_APPID}.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
endif()

# DISCLAIMER is not paperwork here: this is not a medical device, and someone
# who installs a package rather than reading the repository should still be
# able to find that out on their own machine.
install(FILES ${CMAKE_SOURCE_DIR}/README.md
              ${CMAKE_SOURCE_DIR}/DISCLAIMER
              ${CMAKE_SOURCE_DIR}/THIRD_PARTY.md
        DESTINATION ${OPENNCOMM_DOCDIR})
if(WIN32)
  install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION .)
else()
  install(FILES ${CMAKE_SOURCE_DIR}/LICENSE
          DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/openncomm)
endif()

# The licence of everything the package redistributes, verbatim, beside the
# binaries it applies to. A summary in THIRD_PARTY.md is not what Apache-2.0
# section 4 or the MIT notice clause ask for, and espeak-ng is GPL-3.0, which
# additionally wants the source offer that WRITTEN-OFFER carries.
install(FILES ${CMAKE_SOURCE_DIR}/packaging/WRITTEN-OFFER
        DESTINATION ${OPENNCOMM_DOCDIR})

# The weights are redistributed too, now that they ship inside the package,
# and their licences are not the code's. MODEL-NOTICE names each model, where
# it came from and what it is under. It is not documentation: en_US-amy-medium
# is CC-BY-SA-4.0, and that licence requires the credit to travel with the
# voice. See THIRD_PARTY.md.
install(FILES ${CMAKE_SOURCE_DIR}/packaging/MODEL-NOTICE
        DESTINATION ${OPENNCOMM_DOCDIR})
install(FILES
          ${CMAKE_SOURCE_DIR}/third_party/licenses/mediapipe.LICENSE
          ${CMAKE_SOURCE_DIR}/third_party/licenses/piper.LICENSE
          ${CMAKE_SOURCE_DIR}/third_party/licenses/espeak-ng.COPYING
          ${CMAKE_SOURCE_DIR}/third_party/licenses/onnxruntime.LICENSE
          ${CMAKE_SOURCE_DIR}/third_party/licenses/CC-BY-SA-4.0.txt
          ${CMAKE_SOURCE_DIR}/third_party/licenses/CC0-1.0.txt
          ${CMAKE_SOURCE_DIR}/third_party/licenses/qwen2.5.LICENSE
          ${CMAKE_SOURCE_DIR}/third_party/licenses/whisper-model.LICENSE
        DESTINATION ${OPENNCOMM_DOCDIR}/licenses)
# These two are checked out as source, so their licences come from the trees
# themselves rather than being fetched separately.
install(FILES ${LLAMA_DIR}/LICENSE
        DESTINATION ${OPENNCOMM_DOCDIR}/licenses RENAME llama.cpp.LICENSE)
install(FILES ${WHISPER_DIR}/LICENSE
        DESTINATION ${OPENNCOMM_DOCDIR}/licenses RENAME whisper.cpp.LICENSE)

if(WIN32)
  # Qt and OpenCV are redistributed on Windows and on no other platform. The
  # .deb and the .rpm depend on the distribution's copies; Windows has no
  # distribution to depend on, so windeployqt puts Qt's DLLs beside the
  # executable and their terms attach to what ships.
  #
  # Qt stays dynamically linked, in separate files a user can replace with a
  # build of their own, which is what LGPL-3.0 section 4 requires. The licence
  # text is the other half of that requirement. GPL-3.0 -- which LGPL-3.0 is
  # written as a set of additional permissions on top of -- is already here as
  # espeak-ng.COPYING, and is named in THIRD_PARTY.md so nobody has to guess
  # that the two go together.
  install(FILES ${CMAKE_SOURCE_DIR}/third_party/licenses/LGPL-3.0.txt
                ${CMAKE_SOURCE_DIR}/third_party/licenses/opencv.LICENSE
          DESTINATION ${OPENNCOMM_DOCDIR}/licenses)
endif()

# History is the one part outside core/ with logic worth pinning down: what
# counts as an exchange, what a search matches, and above all that clearing
# actually removes the words from the file. It needs no camera, model or
# display, so it runs with the rest of the suites locally. CI builds core/
# standalone and does not reach it.
add_executable(test_history tests/test_history.cc history.cc)
target_link_libraries(test_history PRIVATE Qt6::Core ${OC_SQLITE})
target_include_directories(test_history PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/core/tests)
add_test(NAME history COMMAND test_history)
