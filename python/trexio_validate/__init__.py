"""Validate TREXIO files by recomputing their contents with libcint.

This module wraps the C interface of the trexio-validate library with ctypes.
It validates a TREXIO file that is already open, typically one written with
the in-memory back end, given its ``trexio_t*`` handle::

    import trexio_validate

    report = trexio_validate.validate(handle, require=["mo_orthonormality"])
    print(report)
    report.raise_for_failure()

The handle may be given as

* an integer address, e.g. the value of ``trexio_t*`` exported by a C or C++
  extension (``reinterpret_cast<std::uintptr_t>(file)``);
* a ``ctypes.c_void_p`` or other ctypes pointer;
* a ``PyCapsule`` holding the pointer;
* a ``trexio.File`` object of the TREXIO Python API.

The file is only read, and it is not closed. It must have been opened with the
same TREXIO shared library that trexio-validate is linked to, as the layout of
``trexio_t`` is private to TREXIO. This holds when both the program that
created the file and trexio-validate link to the same ``libtrexio``. It does
not hold for a Python ``trexio`` package that compiles its own copy of the
library into its extension module, as the packages on PyPI do; use
:func:`validate_file` on a file written to disk in that case.

Files on disk are validated with :func:`validate_file`.
"""

import ctypes
import os
from collections import namedtuple

__all__ = [
    "Report",
    "Result",
    "ValidationError",
    "checks",
    "validate",
    "validate_file",
    "version",
]

OK = 0
FAILED = 1
INVALID_ARGUMENT = 2
NOTHING_CHECKED = 77

_STATUS = {0: "pass", 1: "fail", 2: "skip"}


def _load_library():
    path = os.environ.get("TREXIO_VALIDATE_LIBRARY")
    if not path:
        from ._config import library as path
    lib = ctypes.CDLL(path)

    c_char_p, c_int, c_int32, c_double, c_void_p = (
        ctypes.c_char_p, ctypes.c_int, ctypes.c_int32, ctypes.c_double, ctypes.c_void_p)
    signatures = {
        "trexio_validate_version": (c_char_p, []),
        "trexio_validate_check_count": (c_int32, []),
        "trexio_validate_check_name": (c_char_p, [c_int32]),
        "trexio_validate_check_description": (c_char_p, [c_int32]),
        "trexio_validate_options_create": (c_void_p, []),
        "trexio_validate_options_destroy": (None, [c_void_p]),
        "trexio_validate_options_set_tolerance": (c_int, [c_void_p, c_double]),
        "trexio_validate_options_set_check_tolerance": (c_int, [c_void_p, c_char_p, c_double]),
        "trexio_validate_options_select": (c_int, [c_void_p, c_char_p]),
        "trexio_validate_options_skip": (c_int, [c_void_p, c_char_p]),
        "trexio_validate_options_require": (c_int, [c_void_p, c_char_p]),
        "trexio_validate_options_set_max_eri_dim": (c_int, [c_void_p, c_int32]),
        "trexio_validate_report_count": (c_int32, [c_void_p]),
        "trexio_validate_report_name": (c_char_p, [c_void_p, c_int32]),
        "trexio_validate_report_status": (c_int, [c_void_p, c_int32]),
        "trexio_validate_report_detail": (c_char_p, [c_void_p, c_int32]),
        "trexio_validate_report_result": (c_int, [c_void_p]),
        "trexio_validate_report_text": (c_char_p, [c_void_p]),
        "trexio_validate_report_destroy": (None, [c_void_p]),
        "trexio_validate_run": (c_int, [c_void_p, c_void_p, ctypes.POINTER(c_void_p)]),
        "trexio_validate_path": (c_int, [c_char_p, c_void_p, ctypes.POINTER(c_void_p)]),
    }
    for name, (restype, argtypes) in signatures.items():
        function = getattr(lib, name)
        function.restype = restype
        function.argtypes = argtypes
    return lib


_lib = _load_library()


def version():
    """Version of the trexio-validate library."""
    return _lib.trexio_validate_version().decode()


def checks():
    """The available checks, as a dict from name to description."""
    return {
        _lib.trexio_validate_check_name(i).decode(): _lib.trexio_validate_check_description(i).decode()
        for i in range(_lib.trexio_validate_check_count())
    }


Result = namedtuple("Result", ["name", "status", "detail"])
Result.__doc__ = "Outcome of one check; status is 'pass', 'fail' or 'skip'."


class ValidationError(AssertionError):
    """Raised by :meth:`Report.raise_for_failure` when a check failed."""

    def __init__(self, report):
        super().__init__("TREXIO validation failed:\n" + str(report))
        self.report = report


class Report:
    """Results of a validation."""

    def __init__(self, results, result, text):
        self.results = results
        #: OK, FAILED or NOTHING_CHECKED
        self.result = result
        self.text = text

    @property
    def ok(self):
        """True if no check failed (also when nothing could be checked)."""
        return self.result != FAILED

    @property
    def failures(self):
        return [r for r in self.results if r.status == "fail"]

    def raise_for_failure(self):
        """Raise ValidationError if any check failed."""
        if not self.ok:
            raise ValidationError(self)
        return self

    def __bool__(self):
        return self.ok

    def __iter__(self):
        return iter(self.results)

    def __len__(self):
        return len(self.results)

    def __str__(self):
        return self.text

    def __repr__(self):
        counts = {s: sum(r.status == s for r in self.results) for s in ("pass", "fail", "skip")}
        return "<trexio_validate.Report passed=%(pass)d failed=%(fail)d skipped=%(skip)d>" % counts


def _options(tolerance, tolerances, checks, skip, require, max_eri_dim):
    options = _lib.trexio_validate_options_create()
    if not options:
        raise MemoryError()

    def apply(rc, what):
        if rc != OK:
            _lib.trexio_validate_options_destroy(options)
            raise ValueError("invalid option: %s" % what)

    def names(value):
        return [value] if isinstance(value, str) else list(value or [])

    if tolerance is not None:
        apply(_lib.trexio_validate_options_set_tolerance(options, float(tolerance)), "tolerance=%r" % tolerance)
    for name, tol in (tolerances or {}).items():
        apply(_lib.trexio_validate_options_set_check_tolerance(options, name.encode(), float(tol)),
              "tolerances[%r]=%r" % (name, tol))
    for name in names(checks):
        apply(_lib.trexio_validate_options_select(options, name.encode()), "unknown check %r" % name)
    for name in names(skip):
        apply(_lib.trexio_validate_options_skip(options, name.encode()), "unknown check %r" % name)
    for name in names(require):
        apply(_lib.trexio_validate_options_require(options, name.encode()), "unknown check %r" % name)
    if max_eri_dim is not None:
        apply(_lib.trexio_validate_options_set_max_eri_dim(options, int(max_eri_dim)),
              "max_eri_dim=%r" % max_eri_dim)
    return options


def _collect(report):
    try:
        results = [
            Result(_lib.trexio_validate_report_name(report, i).decode(),
                   _STATUS[_lib.trexio_validate_report_status(report, i)],
                   _lib.trexio_validate_report_detail(report, i).decode())
            for i in range(_lib.trexio_validate_report_count(report))
        ]
        return Report(results, _lib.trexio_validate_report_result(report),
                      _lib.trexio_validate_report_text(report).decode())
    finally:
        _lib.trexio_validate_report_destroy(report)


def _run(function, target, tolerance, tolerances, checks, skip, require, max_eri_dim):
    options = _options(tolerance, tolerances, checks, skip, require, max_eri_dim)
    try:
        report = ctypes.c_void_p()
        rc = function(target, options, ctypes.byref(report))
        if not report:
            raise MemoryError() if rc != INVALID_ARGUMENT else ValueError("invalid TREXIO file handle")
        return _collect(report)
    finally:
        _lib.trexio_validate_options_destroy(options)


def _capsule_pointer(capsule):
    api = ctypes.pythonapi
    api.PyCapsule_GetName.restype = ctypes.c_char_p
    api.PyCapsule_GetName.argtypes = [ctypes.py_object]
    api.PyCapsule_GetPointer.restype = ctypes.c_void_p
    api.PyCapsule_GetPointer.argtypes = [ctypes.py_object, ctypes.c_char_p]
    return api.PyCapsule_GetPointer(capsule, api.PyCapsule_GetName(capsule))


def _as_pointer(handle):
    """The address of a trexio_t handle given in one of the supported forms."""
    if isinstance(handle, bool):
        raise TypeError("not a TREXIO file handle: %r" % (handle,))
    if isinstance(handle, int):
        address = handle
    elif isinstance(handle, ctypes.c_void_p):
        address = handle.value
    elif isinstance(handle, ctypes._Pointer):
        address = ctypes.cast(handle, ctypes.c_void_p).value
    elif type(handle).__name__ == "PyCapsule":
        address = _capsule_pointer(handle)
    elif hasattr(handle, "pytrexio_s"):  # trexio.File
        address = int(handle.pytrexio_s.this)
    elif hasattr(handle, "this"):  # SWIG proxy of trexio_s
        address = int(handle.this)
    else:
        raise TypeError("not a TREXIO file handle: %r" % (handle,))
    if not address:
        raise ValueError("null TREXIO file handle")
    return ctypes.c_void_p(address)


_DOC_OPTIONS = """
    Options:
      tolerance    absolute tolerance of all comparisons (default 1e-8)
      tolerances   dict of per-check tolerances
      checks       run only these checks
      skip         do not run these checks
      require      fail instead of skipping these checks when their data is
                   missing; "all" requires every check
      max_eri_dim  largest ao.num/mo.num for which checks needing the full ERI
                   tensor are run (default 64)

    Returns a Report; unknown check names raise ValueError.
"""


def validate(handle, tolerance=None, tolerances=None, checks=None, skip=None, require=None, max_eri_dim=None):
    return _run(_lib.trexio_validate_run, _as_pointer(handle),
                tolerance, tolerances, checks, skip, require, max_eri_dim)


def validate_file(path, tolerance=None, tolerances=None, checks=None, skip=None, require=None, max_eri_dim=None):
    return _run(_lib.trexio_validate_path, os.fsencode(path),
                tolerance, tolerances, checks, skip, require, max_eri_dim)


validate.__doc__ = """Validate an open TREXIO file given its trexio_t handle.
""" + _DOC_OPTIONS
validate_file.__doc__ = """Validate a TREXIO file on disk.
""" + _DOC_OPTIONS
