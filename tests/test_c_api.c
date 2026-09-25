/* Tests of the C interface, compiled as C. */

#include <stdio.h>
#include <string.h>

#include "trexio_validate.h"

static int failures = 0;

#define EXPECT(cond)                                                     \
  do {                                                                   \
    if (!(cond)) {                                                       \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

static int find(const trexio_validate_report_t* report, const char* name) {
  for (int32_t i = 0; i < trexio_validate_report_count(report); ++i) {
    if (strcmp(trexio_validate_report_name(report, i), name) == 0) return i;
  }
  return -1;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    printf("usage: test_c_api GOOD_FILE BAD_NUCLEUS_REPULSION_FILE\n");
    return 2;
  }

  /* The list of checks. */
  EXPECT(trexio_validate_check_count() > 10);
  EXPECT(strcmp(trexio_validate_check_name(0), "basis") == 0);
  EXPECT(trexio_validate_check_name(-1) == NULL);
  EXPECT(trexio_validate_check_name(trexio_validate_check_count()) == NULL);
  EXPECT(strlen(trexio_validate_version()) > 0);

  /* Options reject unknown names and negative values. */
  trexio_validate_options_t* options = trexio_validate_options_create();
  EXPECT(options != NULL);
  EXPECT(trexio_validate_options_select(options, "no_such_check") == TREXIO_VALIDATE_INVALID_ARGUMENT);
  EXPECT(trexio_validate_options_set_tolerance(options, -1.0) == TREXIO_VALIDATE_INVALID_ARGUMENT);
  EXPECT(trexio_validate_options_require(options, "all") == TREXIO_VALIDATE_OK);
  EXPECT(trexio_validate_options_set_tolerance(options, 1e-9) == TREXIO_VALIDATE_OK);

  /* A valid file. */
  trexio_validate_report_t* report = NULL;
  EXPECT(trexio_validate_path(argv[1], options, &report) == TREXIO_VALIDATE_OK);
  EXPECT(report != NULL);
  EXPECT(trexio_validate_report_result(report) == TREXIO_VALIDATE_OK);
  EXPECT(trexio_validate_report_count(report) == trexio_validate_check_count());
  const int i = find(report, "mo_orthonormality");
  EXPECT(i >= 0 && trexio_validate_report_status(report, i) == TREXIO_VALIDATE_PASS);
  EXPECT(strstr(trexio_validate_report_text(report), "PASS  mo_orthonormality") != NULL);
  trexio_validate_report_destroy(report);

  /* A broken file, with only the check that catches it. */
  trexio_validate_options_t* only = trexio_validate_options_create();
  trexio_validate_options_select(only, "nucleus_repulsion");
  EXPECT(trexio_validate_path(argv[2], only, &report) == TREXIO_VALIDATE_FAILED);
  EXPECT(trexio_validate_report_count(report) == 1);
  EXPECT(trexio_validate_report_status(report, 0) == TREXIO_VALIDATE_FAIL);
  trexio_validate_report_destroy(report);

  /* Errors are reported, not thrown. */
  EXPECT(trexio_validate_path("does-not-exist.h5", NULL, &report) == TREXIO_VALIDATE_FAILED);
  EXPECT(find(report, "read") == 0);
  trexio_validate_report_destroy(report);
  EXPECT(trexio_validate_run(NULL, NULL, &report) == TREXIO_VALIDATE_INVALID_ARGUMENT);
  EXPECT(report == NULL);
  EXPECT(trexio_validate_path(argv[1], NULL, NULL) == TREXIO_VALIDATE_OK);

  trexio_validate_options_destroy(only);
  trexio_validate_options_destroy(options);
  if (failures == 0) printf("C interface tests passed\n");
  return failures == 0 ? 0 : 1;
}
