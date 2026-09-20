cmake_minimum_required(VERSION 3.16)

# Single source of truth for the version: python/src/gsusb_canfd/_version.py.
# Both the Python package and the C++/C ABI read it from here.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/python/src/gsusb_canfd/_version.py"
     _canfd_version_line REGEX "__version__")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" CANFD_VERSION "${_canfd_version_line}")
if(NOT CANFD_VERSION)
  message(FATAL_ERROR "could not read __version__ from python/src/gsusb_canfd/_version.py")
endif()

project(gsusb-canfd VERSION ${CANFD_VERSION} LANGUAGES CXX
        DESCRIPTION "Cross-platform userspace gs_usb CAN FD library")

# Only build the tools/examples/tests when this is the top-level project.
# Embedded via add_subdirectory() a parent should get just the library target
# unless it explicitly opts in, and must not have test registration or the
# shared/static choice forced on it.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_canfd_top_level ON)
else()
  set(_canfd_top_level OFF)
endif()

option(CANFD_BUILD_TESTS "Build the test suite" ${_canfd_top_level})
option(CANFD_BUILD_TOOLS "Build the canfd command line tool" ${_canfd_top_level})
option(CANFD_BUILD_EXAMPLES "Build the examples" ${_canfd_top_level})
option(CANFD_BUILD_SHARED
       "Build the library as a shared library (defaults to BUILD_SHARED_LIBS)" ${BUILD_SHARED_LIBS})
option(CANFD_STATIC_RUNTIME
       "Link the C/C++ runtime statically (Windows: keeps libusb-1.0.dll as the only dependency)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CANFD_STATIC_RUNTIME)
  if(MINGW)
    # MinGW otherwise links libwinpthread-1.dll as a separate dependency, so the
    # executables only run where that DLL happens to be on PATH. -static folds
    # libwinpthread, libgcc and libstdc++ into the binaries.
    add_link_options(-static -static-libgcc -static-libstdc++)
  elseif(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
endif()

find_package(Threads REQUIRED)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# --- libusb-1.0 discovery ---------------------------------------------------
# The search logic is shared with the installed package config; see
# cmake/CanFdFindLibusb.cmake for the order and the supported layouts (including
# the official Windows release archive via -DLIBUSB_ROOT=<extracted-dir>).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CanFdFindLibusb.cmake)
canfd_find_libusb()

if(NOT CANFD_LIBUSB_FOUND)
  message(FATAL_ERROR
    "libusb-1.0 not found. Options:\n"
    "  macOS:   brew install libusb\n"
    "  Debian:  sudo apt install libusb-1.0-0-dev\n"
    "  Windows: vcpkg install libusb, or download the official\n"
    "           libusb-1.0.xx.7z from https://github.com/libusb/libusb/releases,\n"
    "           extract it and pass -DLIBUSB_ROOT=<extracted-dir>\n"
    "Searched for libusb.h under: ${LIBUSB_ROOT} (include, include/libusb, include/libusb-1.0)\n"
    "Searched for usb-1.0/libusb-1.0 under: ${LIBUSB_ROOT} "
    "(lib, lib64, lib/x64, x64/Release/dll, MS64/static, MS64, MinGW64/static, MinGW64)\n"
    "Or point directly at the files with -DCANFD_LIBUSB_INCLUDE_DIR=... "
    "-DCANFD_LIBUSB_LIBRARY=...")
endif()

set(CANFD_SOURCES
  src/bit_timing.cpp
  src/frame.cpp
  src/usb_transport.cpp
  src/device.cpp
  src/c_api.cpp)

# The library type follows CANFD_BUILD_SHARED, not BUILD_SHARED_LIBS, so a
# parent project can embed us without having to stash and restore its own
# BUILD_SHARED_LIBS around add_subdirectory().
if(CANFD_BUILD_SHARED)
  add_library(canfd SHARED ${CANFD_SOURCES})
else()
  add_library(canfd STATIC ${CANFD_SOURCES})
endif()
add_library(canfd::canfd ALIAS canfd)

# Generated version header, kept in sync with CANFD_VERSION above.
set(CANFD_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CANFD_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CANFD_VERSION_PATCH ${PROJECT_VERSION_PATCH})
configure_file(cmake/version.hpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/canfd/version.hpp @ONLY)

target_include_directories(canfd
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_features(canfd PUBLIC cxx_std_17)
set_target_properties(canfd PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(canfd PRIVATE canfd_libusb)
target_link_libraries(canfd PRIVATE Threads::Threads)

if(CANFD_BUILD_SHARED)
  target_compile_definitions(canfd PRIVATE CANFD_SHARED)
else()
  target_compile_definitions(canfd PUBLIC CANFD_STATIC)
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

if(CANFD_BUILD_TESTS AND _canfd_top_level)
  enable_testing()

  foreach(test_name test_bit_timing test_frame test_c_api test_adapter)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE canfd::canfd)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()

# --- installation -----------------------------------------------------------
# find_package(gsusb-canfd) then target_link_libraries(app PRIVATE canfd::canfd).
install(TARGETS canfd
  EXPORT gsusb_canfdTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN ".DS_Store" EXCLUDE)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/canfd/version.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/canfd)

install(EXPORT gsusb_canfdTargets
  FILE gsusb-canfd-targets.cmake
  NAMESPACE canfd::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gsusb-canfd)

configure_package_config_file(
  cmake/gsusb-canfd-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/gsusb-canfd-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gsusb-canfd)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/gsusb-canfd-config-version.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/gsusb-canfd-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/gsusb-canfd-config-version.cmake
  cmake/CanFdFindLibusb.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gsusb-canfd)
