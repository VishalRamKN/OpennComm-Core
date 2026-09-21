# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
# One source of truth for the version, in a file the packaging shell scripts can
# read as easily as CMake can. They must agree: a .deb and a .rpm built from the
# same tree that disagree about their version is a support problem nobody can
# see from the outside.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/VERSION OPENNCOMM_VERSION)
string(STRIP "${OPENNCOMM_VERSION}" OPENNCOMM_VERSION)

project(openncomm VERSION ${OPENNCOMM_VERSION} LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# MSVC understands none of these spellings, and /W4 is its nearest equivalent.
# The warnings are not decoration here: -Wshadow is what catches a landmark
# index shadowed inside a loop in core/, which changes what a blink means.
if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  # MSVC's C4996 fires on strncpy and friends throughout core/, which is
  # portable C11 and staying that way. The suggested _s replacements do not
  # exist outside MSVC.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wextra -Wshadow)
endif()

# OpenCV's Windows pack chooses its binary directory by mapping MSVC_VERSION to
# a vcNN folder, and it only knows the toolsets that existed when it was
# released. Visual Studio 18 reports 1951, which OpenCV 4.11 matches against
# nothing: OpenCV_RUNTIME comes out empty, no candidate library path is built,
# and find_package reports
#
#   Found OpenCV Windows Pack but it has no binaries compatible with your
#   configuration.
#
# which reads as OpenCV being absent rather than as a compiler it has not heard
# of. This is what broke the first Windows build, on a runner whose image had
# moved to VS 18.
#
# Setting both OpenCV_ARCH and OpenCV_RUNTIME takes the "custom overridden
# values" branch at the top of OpenCVConfig.cmake and skips the mapping.
#
# Which vcNN to name is NOT a guess -- the pack is probed for the newest one it
# actually carries. Naming a version it does not ship fails in exactly the same
# way as naming none, which is how the first attempt at this went: vc17 was
# set, was reported, and the pack only had vc16. Picking a runtime older than
# the compiler is safe and is what OpenCV's own fallbacks already do for vc15
# and vc16; MSVC has kept binary compatibility since v140. The job that builds
# this also launches the executable, so an ABI that did not hold would fail
# visibly rather than be assumed away.
#
# Set here rather than beside find_package, because src/ and spike/ both look
# for OpenCV and a variable set in one is not visible in the other. Only when
# nothing has been chosen already, so -DOpenCV_RUNTIME still wins; and only for
# a toolset newer than OpenCV knows, so an older Visual Studio keeps using its
# own binaries. x64 because that is the only Windows architecture this ships --
# MediaPipe and Piper arrive as x86_64 binaries.
if(WIN32 AND MSVC AND MSVC_VERSION GREATER 1949
   AND NOT DEFINED OpenCV_RUNTIME AND NOT DEFINED OpenCV_ARCH)
  set(_oc_cv_root "${OpenCV_DIR}")
  if(NOT _oc_cv_root)
    set(_oc_cv_root "$ENV{OpenCV_DIR}")
  endif()
  foreach(_oc_rt vc17 vc16 vc15 vc14)
    if(EXISTS "${_oc_cv_root}/x64/${_oc_rt}/lib")
      set(OpenCV_ARCH x64)
      set(OpenCV_RUNTIME ${_oc_rt})
      message(STATUS
        "OpenCV: MSVC ${MSVC_VERSION} is newer than this OpenCV knows; "
        "using the ${_oc_rt} binaries it ships")
      break()
    endif()
  endforeach()
  if(NOT DEFINED OpenCV_RUNTIME)
    # Left alone deliberately. This is not a Windows pack, or not one with the
    # layout above -- a vcpkg or source build of OpenCV ignores these variables
    # entirely -- so find_package should be allowed to do its normal job and
    # report its own error rather than one invented here.
    message(STATUS "OpenCV: no vcNN binaries under '${_oc_cv_root}/x64'; leaving detection to find_package")
  endif()
  unset(_oc_cv_root)
endif()

# Windows has no FHS: the application is one directory with the executable at
# the top and models\, piper\ and the Qt DLLs beside it. Set before
# GNUInstallDirs, which only fills in what has not already been decided.
if(WIN32)
  set(CMAKE_INSTALL_BINDIR "." CACHE PATH "Windows installs flat, not into bin/")
endif()

include(GNUInstallDirs)

# The vendored runtimes, for every platform. See the file for why they are not
# declared in the directories that use them.
include(cmake/deps.cmake)

enable_testing()
add_subdirectory(core)
add_subdirectory(src)
add_subdirectory(spike)

# CPack drives the Windows installer and the portable archive. The Linux
# packages are built by scripts/build-deb.sh and scripts/build-rpm.sh instead,
# which have to control the dependency metadata CPack cannot express.
if(WIN32)
  include(cmake/windows-package.cmake)
endif()
