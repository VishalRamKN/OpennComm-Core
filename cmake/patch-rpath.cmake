# SPDX-License-Identifier: GPL-3.0-or-later
#
# Rewrite the RUNPATH of every vendored library to $ORIGIN, at install time.
#
# Three separate messes are being cleaned up here:
#
#   llama.cpp and whisper.cpp bake their absolute build-tree directory in. On
#   another machine that path either does not exist -- or, far worse, exists and
#   holds somebody else's libraries.
#
#   mediapipe arrives from a Python wheel with a RUNPATH pointing into Google's
#   internal Bazel layout, which resolves nowhere.
#
#   Both record the absolute path of the tree that built them. Where that path
#   contains a space -- easy to end up with on a desktop -- it is not merely
#   wrong on another machine but unparseable: a RUNPATH is colon-separated with
#   no quoting.
#
# Run as an install(SCRIPT), so `cmake --install` produces a working tree on its
# own and the package builds inherit it rather than reimplementing it.

if(NOT DEFINED OC_PKGLIBDIR_REL)
  message(FATAL_ERROR "patch-rpath.cmake: invoked without OC_PKGLIBDIR_REL")
endif()
if(NOT OC_PATCHELF)
  message(FATAL_ERROR
    "patchelf is required to install OpennComm, and was not found when this "
    "build was configured.\n"
    "  Fedora:  sudo dnf install patchelf\n"
    "  Debian:  sudo apt install patchelf\n"
    "Install it, re-run cmake to pick it up, then install again.")
endif()

set(_pkglibdir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${OC_PKGLIBDIR_REL}")
file(GLOB _libs "${_pkglibdir}/*.so*")

# An empty glob means the libraries were installed somewhere this script is not
# looking -- which is exactly how this went wrong once already, when the prefix
# was resolved at configure time and `--prefix` then moved it. Silence there is
# indistinguishable from success, and ships libraries with a stale absolute
# RUNPATH. Fail instead.
if(NOT _libs)
  message(FATAL_ERROR
    "patch-rpath.cmake: no libraries found in ${_pkglibdir} -- nothing was "
    "patched, which means the install layout moved and this script did not "
    "follow it.")
endif()

foreach(_lib ${_libs})
  # Only the real files. Following the symlinks would patch the same library
  # once per name it is known by.
  if(IS_SYMLINK "${_lib}")
    continue()
  endif()
  execute_process(
    COMMAND "${OC_PATCHELF}" --set-rpath "$ORIGIN" "${_lib}"
    RESULT_VARIABLE _rc ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "patchelf failed on ${_lib}: ${_err}")
  endif()
  message(STATUS "RUNPATH -> $ORIGIN: ${_lib}")
endforeach()
