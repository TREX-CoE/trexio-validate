add_executable(test_conventions test_conventions.cpp)
target_link_libraries(test_conventions PRIVATE trexio_validate_objects)
add_test(NAME conventions COMMAND test_conventions)

set(data "${CMAKE_CURRENT_SOURCE_DIR}/data")

# ---------------------------------------------------------------- valid files
# REQUIRE makes sure that the data we expect in each file is actually checked.

set(common basis nucleus_repulsion electron_count mo_orthonormality)
set(ao_1e ao_1e_int_overlap ao_1e_int_kinetic ao_1e_int_potential_n_e
  ao_1e_int_core_hamiltonian ao_1e_int_dipole_x ao_1e_int_dipole_y ao_1e_int_dipole_z)

trexio_validate_add_test(valid_h2o_631gs_sph FILE "${data}/h2o_631gs_sph.h5"
  REQUIRE all)
trexio_validate_add_test(valid_h2o_631gs_cart FILE "${data}/h2o_631gs_cart.h5"
  REQUIRE ${common} ${ao_1e} mo_1e_int_dipole_x mo_1e_int_dipole_y mo_1e_int_dipole_z
  ao_2e_int_eri)
trexio_validate_add_test(valid_n2_ccpvqz_sph FILE "${data}/n2_ccpvqz_sph.h5"
  REQUIRE ${common})
trexio_validate_add_test(valid_n2_ccpvtz_cart FILE "${data}/n2_ccpvtz_cart.h5"
  REQUIRE ${common})
trexio_validate_add_test(valid_oh_631g_uhf FILE "${data}/oh_631g_uhf.h5"
  REQUIRE all)

# ---------------------------------------------------------------- invalid files
# Each file is broken in one way; only the check that should catch it is run.

function(expect_failure name file check)
  trexio_validate_add_test(${name} FILE "${data}/${file}" CHECKS ${check})
  set_property(TEST ${name} PROPERTY WILL_FAIL TRUE)
endfunction()

expect_failure(invalid_ao_order bad_ao_order.h5 mo_orthonormality)
expect_failure(invalid_ao_normalization bad_ao_normalization.h5 mo_orthonormality)
expect_failure(invalid_nucleus_repulsion bad_nucleus_repulsion.h5 nucleus_repulsion)
expect_failure(invalid_ao_eri_value bad_ao_eri_value.h5 ao_2e_int_eri)
expect_failure(invalid_ao_eri_missing bad_ao_eri_missing.h5 ao_2e_int_eri)
expect_failure(invalid_pyscf_forge_ao_overlap bad_pyscf_forge_ao_integrals.h5 ao_1e_int_overlap)
expect_failure(invalid_pyscf_forge_ao_eri bad_pyscf_forge_ao_integrals.h5 ao_2e_int_eri)

# A required check whose data is missing fails.
trexio_validate_add_test(require_missing_data FILE "${data}/n2_ccpvqz_sph.h5"
  REQUIRE ao_2e_int_eri)
set_property(TEST require_missing_data PROPERTY WILL_FAIL TRUE)

# ---------------------------------------------------------------- command line

add_test(NAME cli_list_checks COMMAND trexio-validate --list-checks)
add_test(NAME cli_bad_option COMMAND trexio-validate --no-such-option x.h5)
set_property(TEST cli_bad_option PROPERTY WILL_FAIL TRUE)
add_test(NAME cli_missing_file COMMAND trexio-validate "${data}/does_not_exist.h5")
set_property(TEST cli_missing_file PROPERTY WILL_FAIL TRUE)

# ---------------------------------------------------------------- library interfaces

if(TREXIO_VALIDATE_LIBRARY)
  enable_language(C)
  add_executable(test_c_api test_c_api.c)
  target_link_libraries(test_c_api PRIVATE trexio_validate)
  add_test(NAME c_api COMMAND test_c_api
    "${data}/h2o_631gs_sph.h5" "${data}/bad_nucleus_repulsion.h5")

  # Plays the part of an application writing TREXIO_MEMORY files. The
  # in-memory back end is newer than TREXIO 2.6.1; without it the tests using
  # it report themselves as skipped.
  add_library(tv_memory_helper SHARED memory_helper.cpp)
  target_link_libraries(tv_memory_helper PRIVATE trexio_validate_objects)
  set_target_properties(tv_memory_helper PROPERTIES CXX_VISIBILITY_PRESET hidden)

  add_executable(test_memory test_memory.cpp)
  target_link_libraries(test_memory PRIVATE trexio_validate tv_memory_helper)
  add_test(NAME memory_backend COMMAND test_memory "${data}/h2o_631gs_sph.h5")
  set_tests_properties(memory_backend PROPERTIES SKIP_RETURN_CODE 77)

  find_package(Python3 COMPONENTS Interpreter)
  if(TREXIO_VALIDATE_PYTHON AND Python3_FOUND)
    add_test(NAME python COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/test_python.py" "${data}" "$<TARGET_FILE:tv_memory_helper>")
    set_tests_properties(python PROPERTIES
      ENVIRONMENT "PYTHONPATH=${TREXIO_VALIDATE_PYTHON_BUILD_DIR}")
  endif()
endif()
