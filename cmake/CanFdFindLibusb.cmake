# Shared libusb-1.0 discovery, used both by the build tree and by the installed
# package config (gsusb-canfd-config.cmake).
#
# It always exposes a single, stable interface target named `canfd_libusb`
# (IMPORTED, so an exported target may reference it and the consumer recreates
# it here). Search order:
#
#   1. an explicit CANFD_LIBUSB_INCLUDE_DIR + CANFD_LIBUSB_LIBRARY pair
#   2. pkg-config
#   3. an installed CMake package (vcpkg, Conan, distro)
#   4. find_path/find_library, honouring LIBUSB_ROOT / CMAKE_PREFIX_PATH
#
# PATH_SUFFIXES in step 4 also cover the official Windows release archive,
# which nests the header as include/libusb/libusb.h and ships import libraries
# under MS64/static, MinGW64/static, ... so -DLIBUSB_ROOT=<archive root> works.
#
# Result: CANFD_LIBUSB_FOUND is set in the caller's scope; when TRUE,
# `canfd_libusb` is defined.

function(canfd_find_libusb)
  if(TARGET canfd_libusb)
    set(CANFD_LIBUSB_FOUND TRUE PARENT_SCOPE)
    return()
  endif()

  set(_include_dirs "")
  set(_link_items "")

  # 1. explicit override (cache hit is used as-is, never searched over).
  if(CANFD_LIBUSB_INCLUDE_DIR AND CANFD_LIBUSB_LIBRARY)
    set(_include_dirs "${CANFD_LIBUSB_INCLUDE_DIR}")
    set(_link_items "${CANFD_LIBUSB_LIBRARY}")
  endif()

  # 2. pkg-config.
  if(NOT _link_items)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(_CANFD_LIBUSB_PC QUIET libusb-1.0)
      if(_CANFD_LIBUSB_PC_FOUND)
        set(_include_dirs "${_CANFD_LIBUSB_PC_INCLUDE_DIRS}")
        if(_CANFD_LIBUSB_PC_LINK_LIBRARIES)
          set(_link_items "${_CANFD_LIBUSB_PC_LINK_LIBRARIES}")
        else()
          set(_link_items "${_CANFD_LIBUSB_PC_LIBRARIES}")
        endif()
      endif()
    endif()
  endif()

  # 3. an installed CMake package.
  if(NOT _link_items)
    find_package(libusb-1.0 CONFIG QUIET)
    foreach(_libusb_target
        libusb::libusb libusb-1.0::libusb-1.0 libusb::usb-1.0 usb-1.0)
      if(TARGET "${_libusb_target}")
        add_library(canfd_libusb INTERFACE IMPORTED)
        target_link_libraries(canfd_libusb INTERFACE "${_libusb_target}")
        set(CANFD_LIBUSB_FOUND TRUE PARENT_SCOPE)
        return()
      endif()
    endforeach()
  endif()

  # 4. find_path / find_library.
  if(NOT _link_items)
    set(LIBUSB_ROOT "" CACHE PATH
        "Root of an extracted/installed libusb (contains include/ and lib/)")
    find_path(CANFD_LIBUSB_INCLUDE_DIR NAMES libusb.h
      HINTS ${LIBUSB_ROOT} ENV LIBUSB_ROOT
      PATH_SUFFIXES include include/libusb include/libusb-1.0)
    find_library(CANFD_LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0
      HINTS ${LIBUSB_ROOT} ENV LIBUSB_ROOT
      PATH_SUFFIXES lib lib64 lib/x64 x64/Release/dll
                    MS64/static MS64 MinGW64/static MinGW64)
    if(CANFD_LIBUSB_INCLUDE_DIR AND CANFD_LIBUSB_LIBRARY)
      set(_include_dirs "${CANFD_LIBUSB_INCLUDE_DIR}")
      set(_link_items "${CANFD_LIBUSB_LIBRARY}")
    endif()
  endif()

  if(_include_dirs AND _link_items)
    add_library(canfd_libusb INTERFACE IMPORTED)
    target_include_directories(canfd_libusb INTERFACE ${_include_dirs})
    target_link_libraries(canfd_libusb INTERFACE ${_link_items})
    set(CANFD_LIBUSB_FOUND TRUE PARENT_SCOPE)
  else()
    set(CANFD_LIBUSB_FOUND FALSE PARENT_SCOPE)
  endif()
endfunction()
