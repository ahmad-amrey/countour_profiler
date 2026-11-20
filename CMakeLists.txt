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
  src/core/fit.cpp
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
    src/app/calsheet.cpp
    src/app/calsheet.h
    src/app/theme.cpp
    src/app/theme.h
    src/app/widgets.cpp
    src/app/widgets.h
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
  add_executable(cp_selftest tests/selftest.cpp src/app/calsheet.cpp)
  target_include_directories(cp_selftest PRIVATE src/app)
  target_link_libraries(cp_selftest PRIVATE cpcore Qt6::Gui)
endif()

# ---------------------------------------------------------------------------
# Tests (ctest): synthetic ground truth + real reference photos of one wire.
# ---------------------------------------------------------------------------
if(CP_BUILD_TESTS)
  enable_testing()
  add_test(NAME line_arc_fitting COMMAND cp_selftest fit)
  add_test(NAME printed_calibration_sheets COMMAND cp_selftest sheet ${CMAKE_BINARY_DIR}/test-output/sheets)
  add_test(NAME synthetic_ground_truth
           COMMAND cp_selftest synth ${CMAKE_BINARY_DIR}/test-output/synth)
  file(GLOB CP_REFERENCE_PHOTOS ${CMAKE_SOURCE_DIR}/tests/data/*.jpg)
  if(CP_REFERENCE_PHOTOS)
    add_test(NAME reference_photos_consistency
             COMMAND cp_selftest consistency ${CMAKE_BINARY_DIR}/test-output/reference 10 ${CP_REFERENCE_PHOTOS})
  endif()
endif()
