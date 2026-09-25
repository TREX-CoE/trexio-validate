/*
 * trexio-validate: check the contents of TREXIO files against integrals
 * recomputed with libcint.
 *
 * The functions below validate a TREXIO file that is already open, given its
 * trexio_t handle, e.g. one written with the in-memory back end
 * (TREXIO_MEMORY), or a file on disk given its path.
 *
 * A handle passed to trexio_validate_run() is only read from, and it is not
 * closed. It must have been opened with the same TREXIO library that this
 * library is linked to: trexio_t is an opaque type whose layout is private to
 * TREXIO, so a handle created by a different copy of the library (for
 * instance, one compiled into another program or Python module) is not
 * compatible in general.
 *
 * No function throws or aborts; errors are reported through return values and
 * in the report.
 */
#ifndef TREXIO_VALIDATE_H
#define TREXIO_VALIDATE_H

#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(TREXIO_VALIDATE_BUILDING)
#    define TREXIO_VALIDATE_API __declspec(dllexport)
#  else
#    define TREXIO_VALIDATE_API __declspec(dllimport)
#  endif
#else
#  define TREXIO_VALIDATE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Inside the extern "C" block: TREXIO releases before 2.3 lack their own. */
#include <trexio.h>

/* Return values of trexio_validate_run(), trexio_validate_path() and
   trexio_validate_report_result(); they are also the exit codes of the
   trexio-validate program. */
#define TREXIO_VALIDATE_OK               0  /* every check that could run passed */
#define TREXIO_VALIDATE_FAILED           1  /* at least one check failed */
#define TREXIO_VALIDATE_INVALID_ARGUMENT 2  /* e.g. an unknown check name */
#define TREXIO_VALIDATE_NOTHING_CHECKED 77  /* no check could be run */

/* Outcome of one check. */
typedef enum {
  TREXIO_VALIDATE_PASS = 0,
  TREXIO_VALIDATE_FAIL = 1,
  TREXIO_VALIDATE_SKIP = 2
} trexio_validate_status_t;

/* Version of trexio-validate, e.g. "0.1.0". */
TREXIO_VALIDATE_API const char* trexio_validate_version(void);

/* ------------------------------------------------------------------ checks */

/* The available checks, in the order they are run. */
TREXIO_VALIDATE_API int32_t trexio_validate_check_count(void);
/* Name and description of check i, or NULL if i is out of range. */
TREXIO_VALIDATE_API const char* trexio_validate_check_name(int32_t i);
TREXIO_VALIDATE_API const char* trexio_validate_check_description(int32_t i);

/* ----------------------------------------------------------------- options */

typedef struct trexio_validate_options_s trexio_validate_options_t;

/* Default options: absolute tolerance 1e-8, all checks, none required. */
TREXIO_VALIDATE_API trexio_validate_options_t* trexio_validate_options_create(void);
TREXIO_VALIDATE_API void trexio_validate_options_destroy(trexio_validate_options_t* options);

/* The setters return TREXIO_VALIDATE_OK, or TREXIO_VALIDATE_INVALID_ARGUMENT
   for a null options pointer, an unknown check name, or a negative value. */

/* Absolute tolerance of all comparisons. */
TREXIO_VALIDATE_API int trexio_validate_options_set_tolerance(trexio_validate_options_t* options,
                                                              double tolerance);
/* Tolerance of a single check, overriding the global one. */
TREXIO_VALIDATE_API int trexio_validate_options_set_check_tolerance(trexio_validate_options_t* options,
                                                                    const char* check, double tolerance);
/* Run this check; once any check is selected, only selected checks run. */
TREXIO_VALIDATE_API int trexio_validate_options_select(trexio_validate_options_t* options, const char* check);
/* Do not run this check. */
TREXIO_VALIDATE_API int trexio_validate_options_skip(trexio_validate_options_t* options, const char* check);
/* Fail this check, instead of skipping it, when its data is missing; "all"
   requires every check. */
TREXIO_VALIDATE_API int trexio_validate_options_require(trexio_validate_options_t* options, const char* check);
/* Largest ao.num / mo.num for which checks needing the full ERI tensor are
   run (default 64). */
TREXIO_VALIDATE_API int trexio_validate_options_set_max_eri_dim(trexio_validate_options_t* options,
                                                                int32_t dim);

/* ------------------------------------------------------------------ report */

typedef struct trexio_validate_report_s trexio_validate_report_t;

/* Number of results, and the name, status and details of result i. The
   strings belong to the report; out-of-range indices give NULL and
   TREXIO_VALIDATE_SKIP. */
TREXIO_VALIDATE_API int32_t trexio_validate_report_count(const trexio_validate_report_t* report);
TREXIO_VALIDATE_API const char* trexio_validate_report_name(const trexio_validate_report_t* report, int32_t i);
TREXIO_VALIDATE_API trexio_validate_status_t trexio_validate_report_status(const trexio_validate_report_t* report,
                                                                          int32_t i);
TREXIO_VALIDATE_API const char* trexio_validate_report_detail(const trexio_validate_report_t* report, int32_t i);
/* Overall result: TREXIO_VALIDATE_OK, _FAILED or _NOTHING_CHECKED. */
TREXIO_VALIDATE_API int trexio_validate_report_result(const trexio_validate_report_t* report);
/* The report as text, one line per check, as printed by trexio-validate. */
TREXIO_VALIDATE_API const char* trexio_validate_report_text(const trexio_validate_report_t* report);
TREXIO_VALIDATE_API void trexio_validate_report_destroy(trexio_validate_report_t* report);

/* -------------------------------------------------------------- validation */

/* Validate an open TREXIO file. options may be NULL for the defaults. If
   report is not NULL, *report receives a report to be released with
   trexio_validate_report_destroy(), also when the file cannot be read (the
   report then contains a failed "read" entry). Returns the overall result, or
   TREXIO_VALIDATE_INVALID_ARGUMENT for a null file handle. */
TREXIO_VALIDATE_API int trexio_validate_run(trexio_t* file, const trexio_validate_options_t* options,
                                            trexio_validate_report_t** report);

/* The same for a file on disk, which is opened and closed here. */
TREXIO_VALIDATE_API int trexio_validate_path(const char* path, const trexio_validate_options_t* options,
                                             trexio_validate_report_t** report);

#ifdef __cplusplus
}
#endif

#endif /* TREXIO_VALIDATE_H */
