# Locate (or, if requested, download and build) the two libraries trexio-validate links to,
# and expose them as the internal targets
#   TrexioValidate_trexio
#   TrexioValidate_cint

include(FetchContent)

set(TREXIO_VALIDATE_TREXIO_URL
  "https://github.com/TREX-CoE/trexio/releases/download/v2.6.1/trexio-2.6.1.tar.gz"
  CACHE STRING "Release tarball used when TREXIO_VALIDATE_FETCH_TREXIO is ON")
set(TREXIO_VALIDATE_LIBCINT_TAG "v6.1.3"
  CACHE STRING "libcint git tag used when TREXIO_VALIDATE_FETCH_LIBCINT is ON")

# ---------------------------------------------------------------- TREXIO

add_library(TrexioValidate_trexio INTERFACE)

function(_trexio_validate_fetch_trexio)
  # Variables set here are only seen by the TREXIO subproject.
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
  set(TREXIO_FORTRAN OFF)  # newer TREXIO
  set(TREXIO_TESTS OFF)    # TREXIO 2.6
  set(BUILD_TESTING OFF)
  set(BUILD_SHARED_LIBS OFF)
  set(BUILD_STATIC_LIBS ON)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  FetchContent_Declare(trexio
    URL "${TREXIO_VALIDATE_TREXIO_URL}"
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(trexio)
  set(trexio_SOURCE_DIR "${trexio_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

if(TREXIO_VALIDATE_FETCH_TREXIO)
  message(STATUS "Building TREXIO from ${TREXIO_VALIDATE_TREXIO_URL}")
  _trexio_validate_fetch_trexio()
  target_link_libraries(TrexioValidate_trexio INTERFACE trexio)
  set(TREXIO_VALIDATE_TREXIO_PROVIDER bundled)
else()
  find_package(trexio CONFIG QUIET)
  if(TARGET trexio::trexio)
    target_link_libraries(TrexioValidate_trexio INTERFACE trexio::trexio)
    set(TREXIO_VALIDATE_TREXIO_PROVIDER config)
    message(STATUS "Found TREXIO (CMake package): ${trexio_DIR}")
  else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(TREXIO_PC QUIET IMPORTED_TARGET trexio)
    endif()
    if(TARGET PkgConfig::TREXIO_PC)
      target_link_libraries(TrexioValidate_trexio INTERFACE PkgConfig::TREXIO_PC)
      set(TREXIO_VALIDATE_TREXIO_PROVIDER pkgconfig)
      message(STATUS "Found TREXIO (pkg-config): ${TREXIO_PC_VERSION}")
    else()
      find_path(TREXIO_INCLUDE_DIR trexio.h)
      find_library(TREXIO_LIBRARY NAMES trexio)
      if(NOT TREXIO_INCLUDE_DIR OR NOT TREXIO_LIBRARY)
        message(FATAL_ERROR
          "TREXIO not found. Point CMAKE_PREFIX_PATH (or TREXIO_INCLUDE_DIR and "
          "TREXIO_LIBRARY) to an installation, or set "
          "TREXIO_VALIDATE_FETCH_TREXIO=ON to build it.")
      endif()
      target_include_directories(TrexioValidate_trexio INTERFACE "${TREXIO_INCLUDE_DIR}")
      target_link_libraries(TrexioValidate_trexio INTERFACE "${TREXIO_LIBRARY}")
      set(TREXIO_VALIDATE_TREXIO_PROVIDER path)
      message(STATUS "Found TREXIO: ${TREXIO_LIBRARY}")
    endif()
  endif()
endif()

# Consumers of the installed library get TREXIO through the imported target
# TrexioValidate::trexio_dependency, which TrexioValidateConfig.cmake creates
# the same way TREXIO was found here.
set(TREXIO_VALIDATE_TREXIO_INSTALL_INTERFACE
  "$<INSTALL_INTERFACE:TrexioValidate::trexio_dependency>")

# ---------------------------------------------------------------- libcint

add_library(TrexioValidate_cint INTERFACE)

function(_trexio_validate_fetch_libcint)
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
  set(BUILD_SHARED_LIBS OFF)
  set(ENABLE_TEST OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  FetchContent_Declare(libcint
    GIT_REPOSITORY https://github.com/sunqm/libcint.git
    GIT_TAG "${TREXIO_VALIDATE_LIBCINT_TAG}"
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(libcint)
  set(libcint_SOURCE_DIR "${libcint_SOURCE_DIR}" PARENT_SCOPE)
  set(libcint_BINARY_DIR "${libcint_BINARY_DIR}" PARENT_SCOPE)
endfunction()

if(TREXIO_VALIDATE_FETCH_LIBCINT)
  message(STATUS "Building libcint ${TREXIO_VALIDATE_LIBCINT_TAG} from GitHub")
  _trexio_validate_fetch_libcint()
  # libcint generates cint.h into its build tree and does not export usage
  # requirements for its headers.
  target_include_directories(TrexioValidate_cint INTERFACE
    "${libcint_BINARY_DIR}/include" "${libcint_SOURCE_DIR}/include")
  target_link_libraries(TrexioValidate_cint INTERFACE cint)
else()
  find_path(LIBCINT_INCLUDE_DIR cint.h)
  find_library(LIBCINT_LIBRARY NAMES cint)
  if(NOT LIBCINT_INCLUDE_DIR OR NOT LIBCINT_LIBRARY)
    message(FATAL_ERROR
      "libcint not found. Point CMAKE_PREFIX_PATH (or LIBCINT_INCLUDE_DIR and "
      "LIBCINT_LIBRARY) to an installation, or set "
      "TREXIO_VALIDATE_FETCH_LIBCINT=ON to build it.")
  endif()
  target_include_directories(TrexioValidate_cint INTERFACE "${LIBCINT_INCLUDE_DIR}")
  target_link_libraries(TrexioValidate_cint INTERFACE "${LIBCINT_LIBRARY}")
  message(STATUS "Found libcint: ${LIBCINT_LIBRARY}")
endif()
