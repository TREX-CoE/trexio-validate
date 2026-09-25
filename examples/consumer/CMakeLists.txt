# Example of a project whose test suite validates the TREXIO files it writes.
#
# With an installed trexio-validate:
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=<prefix>
# Or, to build trexio-validate as part of this project:
#   cmake -S . -B build -DUSE_FETCHCONTENT=ON [-DFETCHCONTENT_SOURCE_DIR_TREXIOVALIDATE=<checkout>]
cmake_minimum_required(VERSION 3.21)
project(trexio_validate_consumer LANGUAGES C CXX)

option(USE_FETCHCONTENT "Build trexio-validate with FetchContent" OFF)

enable_testing()

if(USE_FETCHCONTENT)
  include(FetchContent)
  set(TREXIO_VALIDATE_GIT_URL "https://github.com/TREX-CoE/trexio-validate.git"
    CACHE STRING "Repository of trexio-validate")
  FetchContent_Declare(TrexioValidate
    GIT_REPOSITORY "${TREXIO_VALIDATE_GIT_URL}"
    GIT_TAG master)
  FetchContent_MakeAvailable(TrexioValidate)
else()
  find_package(TrexioValidate REQUIRED)
endif()

# Stand-in for a test of this project that writes a TREXIO file.
add_test(NAME write_trexio
  COMMAND "${CMAKE_COMMAND}" -E copy
          "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/data/h2o_631gs_sph.h5"
          "${CMAKE_CURRENT_BINARY_DIR}/h2o.h5")
set_tests_properties(write_trexio PROPERTIES FIXTURES_SETUP h2o_file)

# Validate everything in the file, and insist that the MOs and the AO
# integrals are there.
trexio_validate_add_test(validate_h2o
  FILE "${CMAKE_CURRENT_BINARY_DIR}/h2o.h5"
  FIXTURES_REQUIRED h2o_file
  REQUIRE mo_orthonormality ao_1e_int_overlap ao_2e_int_eri)
