"""Tests of the Python module.

Usage: test_python.py DATA_DIR [MEMORY_HELPER_LIBRARY]

With the helper library, TREXIO_MEMORY files are created in C, as an
application embedding TREXIO would, and passed to Python as pointers.
"""

import ctypes
import os
import sys

import trexio_validate as tv

failures = 0


def expect(cond, what):
    global failures
    if not cond:
        print("FAIL", what)
        failures += 1


def raises(exception, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except exception:
        return True
    return False


data = sys.argv[1]
good = os.path.join(data, "h2o_631gs_sph.h5")

# Module-level functions.
expect(tv.version(), "version")
checks = tv.checks()
expect("mo_orthonormality" in checks and "ao_2e_int_eri" in checks, "check list")

# Files on disk.
report = tv.validate_file(good, require="all")
expect(report.ok and report.result == tv.OK, "valid file passes")
expect(len(report) == len(checks), "every check ran")
unavailable = {name for name, text in checks.items() if "[unavailable" in text}
expect(all(r.status == "pass" or (r.status == "skip" and r.name in unavailable) for r in report),
       "every available check passed")
expect("PASS  mo_orthonormality" in str(report), "report text")

bad = tv.validate_file(os.path.join(data, "bad_nucleus_repulsion.h5"), checks=["nucleus_repulsion"])
expect(not bad.ok and [r.name for r in bad.failures] == ["nucleus_repulsion"], "broken file fails")
expect(raises(tv.ValidationError, bad.raise_for_failure), "raise_for_failure raises")
expect(raises(ValueError, tv.validate_file, good, checks=["no_such_check"]), "unknown check rejected")
expect(raises(ValueError, tv.validate_file, good, tolerance=-1), "negative tolerance rejected")
expect(not tv.validate_file(os.path.join(data, "missing.h5")).ok, "missing file fails")

# Handles.
expect(raises(ValueError, tv.validate, 0), "null handle rejected")
expect(raises(TypeError, tv.validate, "not a handle"), "wrong type rejected")

if len(sys.argv) > 2:
    helper = ctypes.CDLL(sys.argv[2])
    helper.tv_memory_copy.restype = ctypes.c_void_p
    helper.tv_memory_copy.argtypes = [ctypes.c_char_p, ctypes.c_int]
    helper.tv_memory_close.argtypes = [ctypes.c_void_p]
    helper.tv_memory_nucleus_num.restype = ctypes.c_int32
    helper.tv_memory_nucleus_num.argtypes = [ctypes.c_void_p]
    if not helper.tv_memory_available():
        print("TREXIO has no in-memory back end: memory tests skipped")
        sys.argv = sys.argv[:2]

if len(sys.argv) > 2:
    address = helper.tv_memory_copy(good.encode(), 0)
    expect(address, "memory copy")

    # The same file handle in each of the accepted forms.
    capsule_new = ctypes.pythonapi.PyCapsule_New
    capsule_new.restype = ctypes.py_object
    capsule_new.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p]
    forms = {
        "int": address,
        "c_void_p": ctypes.c_void_p(address),
        "ctypes pointer": ctypes.cast(address, ctypes.POINTER(ctypes.c_char)),
        "capsule": capsule_new(address, b"trexio_t", None),
    }
    for name, handle in forms.items():
        r = tv.validate(handle, require="all")
        expect(r.result == tv.OK, "memory file passes (%s)" % name)
    expect(helper.tv_memory_nucleus_num(address) == 3, "handle still usable")
    helper.tv_memory_close(address)

    for corruption, check in [(1, "nucleus_repulsion"), (2, "mo_orthonormality")]:
        address = helper.tv_memory_copy(good.encode(), corruption)
        r = tv.validate(address)
        expect(check in [f.name for f in r.failures], "corrupted memory file fails %s" % check)
        helper.tv_memory_close(address)

print("Python tests passed" if failures == 0 else "%d failures" % failures)
sys.exit(1 if failures else 0)
