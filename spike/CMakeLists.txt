# SPDX-License-Identifier: GPL-3.0-or-later
find_package(OpenCV REQUIRED COMPONENTS core videoio imgproc)

# mediapipe_c, llama_c and whisper_c are declared once in cmake/deps.cmake, for
# every platform, and imported GLOBAL so they are visible here too.

# The raw-capture spike opens /dev/video0 directly and reports the frame rate
# the landmarker actually sustains. It is a Linux development tool and has no
# Windows equivalent worth writing: on Windows the same measurement comes from
# the application's own low-frame-rate warning. See src/camera.cc.
if(NOT WIN32)
  add_executable(landmark_fps landmark_fps.cc)
  target_link_libraries(landmark_fps PRIVATE oc_core mediapipe_c ${OpenCV_LIBS})
  target_include_directories(landmark_fps PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()

# Widgets as well as Core, although only answer_test below uses Core alone.
#
# ui_shot links openncomm_lib, which carries Qt6::Widgets, and Qt's executable
# finalizer then resolves Gui's dependencies -- including OpenGL::GLX. Those
# targets are created by Qt6Gui's own find_dependency(OpenGL), which had only
# ever run in src/, and imported targets from another directory are not visible
# here. On Qt 6.10 the finalizer no longer asks, so this configured fine on the
# machine it was written on and failed on Qt 6.4 with an error naming OpenGL
# and nothing else.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets)
qt_add_executable(answer_test answer_test.cc ${CMAKE_SOURCE_DIR}/src/generator.cc)
target_link_libraries(answer_test PRIVATE oc_core llama_c Qt6::Core)
target_include_directories(answer_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(answer_test PROPERTIES AUTOMOC ON)

# Speech-recognition spike: transcribe a raw 16kHz clip, no microphone needed.
add_executable(hear_test hear_test.cc)
target_link_libraries(hear_test PRIVATE whisper_c)

# Offscreen screenshot tool, for looking at the layout during development.
qt_add_executable(ui_shot ui_shot.cc)
target_link_libraries(ui_shot PRIVATE openncomm_lib)
set_target_properties(ui_shot PROPERTIES AUTOMOC ON)
