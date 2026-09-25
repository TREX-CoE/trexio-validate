# The Python module is pure Python: it loads libtrexio_validate with ctypes.
# _config.py records where the library is, relative to the installed module
# and as an absolute path in the build tree.

find_package(Python3 COMPONENTS Interpreter)

if(Python3_FOUND)
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c
      "import sysconfig; print(sysconfig.get_path('purelib', vars={'base': '', 'platbase': ''}).lstrip('/\\\\'))"
    OUTPUT_VARIABLE _purelib OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
  set(_purelib "${CMAKE_INSTALL_LIBDIR}/python3/site-packages")
endif()
set(TREXIO_VALIDATE_PYTHON_INSTALL_DIR "${_purelib}" CACHE STRING
  "Installation directory of the Python module, relative to CMAKE_INSTALL_PREFIX")

set(_module "${CMAKE_CURRENT_SOURCE_DIR}/trexio_validate/__init__.py")

# Build tree, for the tests.
set(TREXIO_VALIDATE_PYTHON_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE INTERNAL "")
configure_file("${_module}" "${CMAKE_CURRENT_BINARY_DIR}/trexio_validate/__init__.py" COPYONLY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/trexio_validate/_config.py"
  CONTENT "library = r\"$<TARGET_FILE:trexio_validate>\"\n")

# Install tree.
if(TREXIO_VALIDATE_INSTALL)
  if(IS_ABSOLUTE "${TREXIO_VALIDATE_PYTHON_INSTALL_DIR}")
    set(_pydir "${TREXIO_VALIDATE_PYTHON_INSTALL_DIR}/trexio_validate")
  else()
    set(_pydir "${CMAKE_INSTALL_PREFIX}/${TREXIO_VALIDATE_PYTHON_INSTALL_DIR}/trexio_validate")
  endif()
  if(WIN32)
    set(_libdir "${CMAKE_INSTALL_FULL_BINDIR}")
    set(_libname "$<TARGET_FILE_NAME:trexio_validate>")
  else()
    set(_libdir "${CMAKE_INSTALL_FULL_LIBDIR}")
    set(_libname "$<TARGET_SONAME_FILE_NAME:trexio_validate>")
  endif()
  file(RELATIVE_PATH _rel "${_pydir}" "${_libdir}")
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/install/_config.py"
    CONTENT "import os

library = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                        \"${_rel}\", \"${_libname}\"))
")
  install(FILES "${_module}" "${CMAKE_CURRENT_BINARY_DIR}/install/_config.py"
    DESTINATION "${TREXIO_VALIDATE_PYTHON_INSTALL_DIR}/trexio_validate")
endif()
