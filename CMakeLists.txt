cmake_minimum_required(VERSION 3.21)

project(TrexioValidate
  VERSION 0.1.0
  DESCRIPTION "Validate TREXIO files by recomputing their contents with libcint"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/modules")

include(GNUInstallDirs)
include(CMakeDependentOption)

option(TREXIO_VALIDATE_FETCH_TREXIO
  "Build a bundled TREXIO instead of using an installed one" OFF)
option(TREXIO_VALIDATE_FETCH_LIBCINT
  "Build a bundled libcint instead of using an installed one" OFF)
# The library validates open trexio_t handles, which must come from the TREXIO
# library the application uses. A bundled TREXIO is a separate copy, so the
# library is only built against an installed TREXIO.
cmake_dependent_option(TREXIO_VALIDATE_LIBRARY
  "Build the trexio_validate library (C, C++ and Python interfaces)" ON
  "NOT TREXIO_VALIDATE_FETCH_TREXIO" OFF)
cmake_dependent_option(TREXIO_VALIDATE_PYTHON
  "Install the trexio_validate Python module" ON
  "TREXIO_VALIDATE_LIBRARY" OFF)
cmake_dependent_option(TREXIO_VALIDATE_BUILD_TESTS
  "Build the tests of trexio-validate itself" ON
  "PROJECT_IS_TOP_LEVEL" OFF)
cmake_dependent_option(TREXIO_VALIDATE_INSTALL
  "Install trexio-validate and its CMake package" ON
  "PROJECT_IS_TOP_LEVEL" OFF)

# ---------------------------------------------------------------- dependencies

include(TrexioValidateDependencies)

# ---------------------------------------------------------------- program and library

add_subdirectory(src)
if(TREXIO_VALIDATE_PYTHON)
  add_subdirectory(python)
endif()
if(TREXIO_VALIDATE_FETCH_TREXIO)
  message(STATUS "trexio_validate library: disabled, since TREXIO is bundled")
endif()

# The helper functions are usable right away by a project that pulls us in
# with add_subdirectory() or FetchContent.
include("${PROJECT_SOURCE_DIR}/cmake/TrexioValidate.cmake")

# ---------------------------------------------------------------- tests

if(TREXIO_VALIDATE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------- install

if(TREXIO_VALIDATE_INSTALL)
  include(CMakePackageConfigHelpers)
  set(_cmakedir "${CMAKE_INSTALL_LIBDIR}/cmake/TrexioValidate")

  install(TARGETS trexio-validate
    EXPORT TrexioValidateTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  if(TREXIO_VALIDATE_LIBRARY)
    install(TARGETS trexio_validate
      EXPORT TrexioValidateTargets
      LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
      ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES
      "${PROJECT_SOURCE_DIR}/include/trexio_validate.h"
      "${PROJECT_SOURCE_DIR}/include/trexio_validate.hpp"
      DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  endif()
  install(EXPORT TrexioValidateTargets
    NAMESPACE TrexioValidate::
    DESTINATION "${_cmakedir}")

  configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/TrexioValidateConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/TrexioValidateConfig.cmake"
    INSTALL_DESTINATION "${_cmakedir}")
  write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/TrexioValidateConfigVersion.cmake"
    COMPATIBILITY SameMinorVersion)
  install(FILES
    "${PROJECT_BINARY_DIR}/TrexioValidateConfig.cmake"
    "${PROJECT_BINARY_DIR}/TrexioValidateConfigVersion.cmake"
    "${PROJECT_SOURCE_DIR}/cmake/TrexioValidate.cmake"
    DESTINATION "${_cmakedir}")
  install(FILES LICENSE README.md DESTINATION "${CMAKE_INSTALL_DOCDIR}")
endif()
