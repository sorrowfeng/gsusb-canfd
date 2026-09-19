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

option(CANFD_BUILD_TESTS "Build the test suite" ON)
option(CANFD_BUILD_TOOLS "Build the canfd command line tool" ON)
option(CANFD_BUILD_EXAMPLES "Build the examples" ON)
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

# --- libusb-1.0 discovery ---------------------------------------------------
# Search order:
#   1. pkg-config (Homebrew, apt, MSYS2, vcpkg with PKG_CONFIG_PATH)
#   2. an installed CMake package (vcpkg, Conan, distro)
#   3. find_path/find_library, honouring LIBUSB_ROOT / CMAKE_PREFIX_PATH
#
# On Windows without a package manager: download the official prebuilt
# libusb-1.0.xx.7z from https://github.com/libusb/libusb/releases, extract it
# and pass -DLIBUSB_ROOT=<extracted-dir>.
set(CANFD_LIBUSB_TARGET "")

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBUSB QUIET IMPORTED_TARGET libusb-1.0)
  if(LIBUSB_FOUND)
    set(CANFD_LIBUSB_TARGET PkgConfig::LIBUSB)
  endif()
endif()

if(NOT CANFD_LIBUSB_TARGET)
  find_package(libusb-1.0 CONFIG QUIET)
  foreach(_libusb_target
      libusb::libusb libusb-1.0::libusb-1.0 libusb::usb-1.0 usb-1.0)
    if(TARGET "${_libusb_target}")
      set(CANFD_LIBUSB_TARGET "${_libusb_target}")
      break()
    endif()
  endforeach()
endif()

if(NOT CANFD_LIBUSB_TARGET)
  set(LIBUSB_ROOT "" CACHE PATH
      "Root of an extracted/installed libusb (contains include/ and lib/)")
  find_path(CANFD_LIBUSB_INCLUDE_DIR NAMES libusb.h
    HINTS ${LIBUSB_ROOT} ENV LIBUSB_ROOT
    PATH_SUFFIXES include include/libusb-1.0)
  find_library(CANFD_LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0
    HINTS ${LIBUSB_ROOT} ENV LIBUSB_ROOT
    PATH_SUFFIXES lib lib64 lib/x64 x64/Release/dll)
  if(CANFD_LIBUSB_INCLUDE_DIR AND CANFD_LIBUSB_LIBRARY)
    add_library(canfd_libusb INTERFACE)
    target_include_directories(canfd_libusb INTERFACE "${CANFD_LIBUSB_INCLUDE_DIR}")
    target_link_libraries(canfd_libusb INTERFACE "${CANFD_LIBUSB_LIBRARY}")
    set(CANFD_LIBUSB_TARGET canfd_libusb)
  endif()
endif()

if(NOT CANFD_LIBUSB_TARGET)
  message(FATAL_ERROR
    "libusb-1.0 not found. Options:\n"
    "  macOS:   brew install libusb\n"
    "  Debian:  sudo apt install libusb-1.0-0-dev\n"
    "  Windows: vcpkg install libusb, or download the official\n"
    "           libusb-1.0.xx.7z from https://github.com/libusb/libusb/releases,\n"
    "           extract it and pass -DLIBUSB_ROOT=<extracted-dir>")
endif()

add_library(canfd
  src/bit_timing.cpp
  src/frame.cpp
  src/usb_transport.cpp
  src/device.cpp
  src/c_api.cpp)
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
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>)

target_compile_features(canfd PUBLIC cxx_std_17)
set_target_properties(canfd PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(canfd PRIVATE ${CANFD_LIBUSB_TARGET})
target_link_libraries(canfd PRIVATE Threads::Threads)

if(BUILD_SHARED_LIBS)
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

if(CANFD_BUILD_TESTS)
  enable_testing()

  foreach(test_name test_bit_timing test_frame test_c_api)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE canfd::canfd)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()
