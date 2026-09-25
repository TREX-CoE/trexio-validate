set(_warnings $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>)

# The implementation, compiled once for both the program and the library.
add_library(trexio_validate_objects OBJECT
  c_api.cpp
  checks.cpp
  integrals.cpp
  solid_harmonics.cpp
  trexio_data.cpp)
set_target_properties(trexio_validate_objects PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
target_include_directories(trexio_validate_objects PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(trexio_validate_objects PRIVATE
  TREXIO_VALIDATE_BUILDING
  TREXIO_VALIDATE_VERSION="${PROJECT_VERSION}")
target_link_libraries(trexio_validate_objects PUBLIC
  TrexioValidate_trexio TrexioValidate_cint)
target_compile_options(trexio_validate_objects PRIVATE ${_warnings})

# The program carries its own copy, so that it works in every configuration.
add_executable(trexio-validate main.cpp)
target_link_libraries(trexio-validate PRIVATE trexio_validate_objects)
target_compile_options(trexio-validate PRIVATE ${_warnings})
# Keep finding TREXIO and libcint after installation when they live outside
# the system library directories.
set_target_properties(trexio-validate PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
add_executable(TrexioValidate::trexio-validate ALIAS trexio-validate)

# The library, exporting the C interface of trexio_validate.h only.
if(TREXIO_VALIDATE_LIBRARY)
  add_library(trexio_validate SHARED)
  target_link_libraries(trexio_validate
    PRIVATE $<BUILD_INTERFACE:trexio_validate_objects>
    PUBLIC  $<BUILD_INTERFACE:TrexioValidate_trexio> ${TREXIO_VALIDATE_TREXIO_INSTALL_INTERFACE})
  target_include_directories(trexio_validate PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  set_target_properties(trexio_validate PROPERTIES
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    INSTALL_RPATH_USE_LINK_PATH TRUE)
  # The C++ standard library's template instantiations keep default
  # visibility; a version script hides them where the linker supports it.
  include(CheckLinkerFlag)
  set(_map "${CMAKE_CURRENT_SOURCE_DIR}/trexio_validate.map")
  check_linker_flag(CXX "-Wl,--version-script=${_map}" TREXIO_VALIDATE_HAVE_VERSION_SCRIPT)
  if(TREXIO_VALIDATE_HAVE_VERSION_SCRIPT)
    target_link_options(trexio_validate PRIVATE "-Wl,--version-script=${_map}")
    set_property(TARGET trexio_validate APPEND PROPERTY LINK_DEPENDS "${_map}")
  endif()
  add_library(TrexioValidate::trexio_validate ALIAS trexio_validate)
endif()
