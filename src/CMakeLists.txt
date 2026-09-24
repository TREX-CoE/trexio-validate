add_library(trexio_validate_core STATIC
  checks.cpp
  integrals.cpp
  solid_harmonics.cpp
  trexio_data.cpp)
target_include_directories(trexio_validate_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(trexio_validate_core PUBLIC
  TrexioValidate_trexio TrexioValidate_cint)
target_compile_options(trexio_validate_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>)

add_executable(trexio-validate main.cpp)
target_link_libraries(trexio-validate PRIVATE trexio_validate_core)
target_compile_options(trexio-validate PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>)

add_executable(TrexioValidate::trexio-validate ALIAS trexio-validate)

# Keep finding TREXIO and libcint after installation when they live outside
# the system library directories.
set_target_properties(trexio-validate PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
