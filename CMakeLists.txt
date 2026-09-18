cmake_minimum_required(VERSION 3.16)

project(gsusb-canfd VERSION 0.1.0 LANGUAGES CXX
        DESCRIPTION "Cross-platform userspace gs_usb CAN FD library for C++")

option(CANFD_BUILD_TESTS "Build the test suite" ON)
option(CANFD_BUILD_TOOLS "Build the canfd command line tool" ON)
option(CANFD_BUILD_EXAMPLES "Build the examples" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

find_package(PkgConfig QUIET)
set(CANFD_HAVE_PKG_CONFIG_LIBUSB OFF)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBUSB QUIET IMPORTED_TARGET libusb-1.0)
  if(LIBUSB_FOUND)
    set(CANFD_HAVE_PKG_CONFIG_LIBUSB ON)
  endif()
endif()

if(NOT CANFD_HAVE_PKG_CONFIG_LIBUSB)
  find_path(CANFD_LIBUSB_INCLUDE_DIR NAMES libusb.h PATH_SUFFIXES libusb-1.0)
  find_library(CANFD_LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0)
  if(NOT CANFD_LIBUSB_INCLUDE_DIR OR NOT CANFD_LIBUSB_LIBRARY)
    message(FATAL_ERROR
      "libusb-1.0 not found. Install it first:\n"
      "  macOS:   brew install libusb\n"
      "  Debian:  sudo apt install libusb-1.0-0-dev\n"
      "  Windows: vcpkg install libusb")
  endif()
endif()

add_library(canfd
  src/bit_timing.cpp
  src/frame.cpp
  src/usb_transport.cpp
  src/device.cpp
  src/c_api.cpp)
add_library(canfd::canfd ALIAS canfd)

target_include_directories(canfd
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(canfd PUBLIC cxx_std_17)
set_target_properties(canfd PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CANFD_HAVE_PKG_CONFIG_LIBUSB)
  target_link_libraries(canfd PRIVATE PkgConfig::LIBUSB)
else()
  target_include_directories(canfd PRIVATE ${CANFD_LIBUSB_INCLUDE_DIR})
  target_link_libraries(canfd PRIVATE ${CANFD_LIBUSB_LIBRARY})
endif()
target_link_libraries(canfd PRIVATE Threads::Threads)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(canfd PRIVATE CANFD_BUILD_SHARED)
  set_target_properties(canfd PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
endif()

if(MSVC)
  target_compile_options(canfd PRIVATE /W4)
else()
  target_compile_options(canfd PRIVATE -Wall -Wextra)
endif()

if(CANFD_BUILD_TOOLS)
  add_executable(canfd_cli tools/canfd_cli.cpp)
  set_target_properties(canfd_cli PROPERTIES OUTPUT_NAME canfd)
  target_link_libraries(canfd_cli PRIVATE canfd::canfd)

  add_executable(canfd_term tools/canfd_term.cpp)
  target_link_libraries(canfd_term PRIVATE canfd::canfd)
endif()

if(CANFD_BUILD_EXAMPLES)
  add_executable(example_trigger_0x501 examples/trigger_0x501.cpp)
  target_link_libraries(example_trigger_0x501 PRIVATE canfd::canfd)

  add_executable(canfd_demo examples/canfd_demo.cpp)
  target_link_libraries(canfd_demo PRIVATE canfd::canfd)
endif()

if(CANFD_BUILD_TESTS)
  enable_testing()

  foreach(test_name test_bit_timing test_frame test_c_api)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE canfd::canfd)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()
