cmake_minimum_required(VERSION 3.16)
project(ContourProfiler VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(CP_BUILD_GUI   "Build the Qt GUI application" ON)
option(CP_BUILD_TESTS "Build the command-line self-test tool" ON)

find_package(Threads REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# ---------------------------------------------------------------------------
# Core library: pure C++17, no Qt. Image processing, homography, detection,
# export. Portable and testable on its own.
# ---------------------------------------------------------------------------
add_library(cpcore STATIC
  src/core/geom.cpp
  src/core/image.cpp
  src/core/grid_detect.cpp
  src/core/profile_detect.cpp
  src/core/calib.cpp
  src/core/export.cpp
  src/core/synth.cpp
)
target_include_directories(cpcore PUBLIC src/core)
target_link_libraries(cpcore PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# GUI application (Qt 6 Widgets)
# ---------------------------------------------------------------------------
if(CP_BUILD_GUI)
  set(CMAKE_AUTOMOC ON)
  add_executable(ContourProfiler WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/app/mainwindow.cpp
    src/app/mainwindow.h
    src/app/canvas.cpp
    src/app/canvas.h
    src/app/document.h
  )
  target_link_libraries(ContourProfiler PRIVATE cpcore Qt6::Widgets)
  set_target_properties(ContourProfiler PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.contourprofiler.app
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
  install(TARGETS ContourProfiler BUNDLE DESTINATION . RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------------------
# Self-test: synthetic ground-truth checks + batch runner for real photos.
# ---------------------------------------------------------------------------
if(CP_BUILD_TESTS)
  add_executable(cp_selftest tests/selftest.cpp)
  target_link_libraries(cp_selftest PRIVATE cpcore Qt6::Gui)
endif()
