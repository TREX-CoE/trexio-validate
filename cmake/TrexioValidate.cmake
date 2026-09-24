#[=======================================================================[.rst:
TrexioValidate
----------------

Helpers for validating TREXIO files from a CTest test suite.

.. command:: trexio_validate_add_test

  Register a CTest test that runs ``trexio-validate`` on a TREXIO file::

    trexio_validate_add_test(<name>
      FILE <path>
      [TOLERANCE <value>]
      [CHECKS <check>...]
      [SKIP <check>...]
      [REQUIRE <check>...]
      [FIXTURES_REQUIRED <fixture>...]
      [DEPENDS <test>...]
      [LABELS <label>...]
      [WORKING_DIRECTORY <dir>]
      [EXTRA_ARGS <arg>...])

  ``FILE``
    The TREXIO file (HDF5 file or text-backend directory) to validate.
    Typically it is produced by another test of the project; use
    ``FIXTURES_REQUIRED`` or ``DEPENDS`` so that the producing test runs first.
  ``TOLERANCE``
    Absolute tolerance used for all numerical comparisons.
  ``CHECKS``
    Run only these checks (see ``trexio-validate --list-checks``).
  ``SKIP``
    Do not run these checks.
  ``REQUIRE``
    Fail if the data needed by these checks is missing from the file,
    instead of skipping them.  ``REQUIRE all`` requires everything that can
    be checked.
  ``EXTRA_ARGS``
    Passed verbatim to ``trexio-validate``.

  The test runs ``TrexioValidate::trexio-validate``, which is available
  both after ``find_package(TrexioValidate)`` and after pulling the project
  in with ``add_subdirectory()`` or ``FetchContent``.
#]=======================================================================]

include_guard(GLOBAL)

function(trexio_validate_add_test name)
  cmake_parse_arguments(PARSE_ARGV 1 arg
    ""
    "FILE;TOLERANCE;WORKING_DIRECTORY"
    "CHECKS;SKIP;REQUIRE;FIXTURES_REQUIRED;DEPENDS;LABELS;EXTRA_ARGS")

  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "trexio_validate_add_test: unknown arguments ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT arg_FILE)
    message(FATAL_ERROR "trexio_validate_add_test: FILE is required")
  endif()
  if(NOT TARGET TrexioValidate::trexio-validate)
    message(FATAL_ERROR
      "trexio_validate_add_test: target TrexioValidate::trexio-validate "
      "does not exist; call find_package(TrexioValidate) first")
  endif()

  set(cmd_args)
  if(DEFINED arg_TOLERANCE)
    list(APPEND cmd_args --tolerance "${arg_TOLERANCE}")
  endif()
  if(arg_CHECKS)
    list(JOIN arg_CHECKS "," joined)
    list(APPEND cmd_args --checks "${joined}")
  endif()
  if(arg_SKIP)
    list(JOIN arg_SKIP "," joined)
    list(APPEND cmd_args --skip "${joined}")
  endif()
  if(arg_REQUIRE)
    list(JOIN arg_REQUIRE "," joined)
    list(APPEND cmd_args --require "${joined}")
  endif()
  list(APPEND cmd_args ${arg_EXTRA_ARGS})

  set(wd_args)
  if(arg_WORKING_DIRECTORY)
    set(wd_args WORKING_DIRECTORY "${arg_WORKING_DIRECTORY}")
  endif()

  add_test(NAME "${name}"
    COMMAND TrexioValidate::trexio-validate ${cmd_args} "${arg_FILE}"
    ${wd_args})

  # trexio-validate exits with 77 when nothing in the file could be checked.
  set_property(TEST "${name}" PROPERTY SKIP_RETURN_CODE 77)
  if(arg_FIXTURES_REQUIRED)
    set_property(TEST "${name}" APPEND PROPERTY
      FIXTURES_REQUIRED ${arg_FIXTURES_REQUIRED})
  endif()
  if(arg_DEPENDS)
    set_property(TEST "${name}" APPEND PROPERTY DEPENDS ${arg_DEPENDS})
  endif()
  if(arg_LABELS)
    set_property(TEST "${name}" APPEND PROPERTY LABELS ${arg_LABELS})
  endif()
endfunction()
