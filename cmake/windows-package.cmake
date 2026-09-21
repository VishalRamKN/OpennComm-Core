# SPDX-License-Identifier: GPL-3.0-or-later
#
# The Windows installer and the portable archive.
#
# Two artifacts from one set of install() rules, because they answer two
# different situations and neither covers the other:
#
#   The .exe installer is what a caregiver should use. It puts an entry in the
#   Start Menu, registers an uninstaller, and needs no explanation beyond
#   "run it".
#
#   The .zip is for the machines where that is not possible -- a ward computer
#   where nobody has an administrator password, or a laptop locked down by an
#   IT department. It unpacks anywhere, including a USB stick, and runs from
#   there. src/paths.cc looks for the models beside the executable precisely so
#   this works.
#
# Both carry the models. See the long note in src/CMakeLists.txt for why that
# is worth 1.4 GB.

set(CPACK_PACKAGE_NAME "OpennComm")
set(CPACK_PACKAGE_VENDOR "OpennComm contributors")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Face-tracked communication for people who cannot speak")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "OpennComm")
set(CPACK_PACKAGE_FILE_NAME "OpennComm-${PROJECT_VERSION}-windows-x64")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)

# NSIS for the installer, ZIP for the portable copy. Both are produced by one
# `cpack` run so they cannot drift apart in content.
set(CPACK_GENERATOR "NSIS;ZIP")

# The archive unpacks into a directory of its own rather than spraying the
# models into whatever folder it was opened in.
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)

# ---- NSIS -------------------------------------------------------------------

# LZMA over the default zlib. The payload is 1.3 GB of already-quantised
# weights, which barely compress either way, but the ~200 MB of Qt, OpenCV and
# MediaPipe binaries do -- and that difference is a download a caregiver is
# waiting on.
set(CPACK_NSIS_COMPRESSOR "/SOLID lzma")

set(CPACK_NSIS_PACKAGE_NAME "OpennComm ${PROJECT_VERSION}")
set(CPACK_NSIS_DISPLAY_NAME "OpennComm")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/vishalramkn/OpennComm")
set(CPACK_NSIS_HELP_LINK "https://github.com/vishalramkn/OpennComm")

# The Start Menu entry and the desktop shortcut. Without the first of these the
# installer produces a folder full of DLLs and no obvious way to start
# anything, which for the person setting this up at a bedside is the same as it
# not working.
set(CPACK_PACKAGE_EXECUTABLES "openncomm;OpennComm")
set(CPACK_CREATE_DESKTOP_LINKS "openncomm")
# CPack's NSIS template looks for the executable in bin/ unless told otherwise,
# and this install is flat -- openncomm.exe sits at the top. Left at the
# default, the installer succeeds and leaves a Start Menu shortcut pointing at
# a path that does not exist, which is a worse failure than not creating one:
# it looks installed and does nothing.
set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
set(CPACK_NSIS_MUI_ICON ${CMAKE_SOURCE_DIR}/packaging/openncomm.ico)
set(CPACK_NSIS_MUI_UNIICON ${CMAKE_SOURCE_DIR}/packaging/openncomm.ico)
set(CPACK_NSIS_INSTALLED_ICON_NAME "openncomm.exe")

# An uninstall that leaves 1.4 GB of models behind is not an uninstall. CPack's
# generated uninstaller removes only the files it recorded, and the models are
# among them, but the directories they sat in are not.
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
  RMDir /r '$INSTDIR\\models'
  RMDir /r '$INSTDIR\\piper'
  RMDir /r '$INSTDIR\\doc'
")

# 64-bit Program Files, not the 32-bit one. MediaPipe, llama.cpp and Qt are all
# x64 here and there is no 32-bit build to confuse this with, so the default
# would simply put an x64 application in the directory reserved for x86.
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

include(CPack)
