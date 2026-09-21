# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
# 3.21 on Windows, for TARGET_RUNTIME_DLLS: it is what collects the Qt, OpenCV
# and sqlite3 DLLs off the targets actually linked, instead of a hand-written
# list that forgets the dependency added last week. There is no equivalent to
# fall back to, so this is a floor rather than a preference.
if(WIN32)
  cmake_minimum_required(VERSION 3.21)
endif()
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
