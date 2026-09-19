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

add_compile_options(-Wall -Wextra -Wshadow)

include(GNUInstallDirs)
enable_testing()
add_subdirectory(core)
add_subdirectory(src)
add_subdirectory(spike)
