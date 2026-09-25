// Validates a TREXIO file through an open handle, as an application would do
// with the file it is writing (e.g. with the TREXIO_MEMORY back end).
#include <cstdio>

#include <trexio_validate.hpp>

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  trexio_exit_code rc = TREXIO_SUCCESS;
  trexio_t* file = trexio_open(argv[1], 'r', TREXIO_AUTO, &rc);
  if (file == nullptr) return 2;

  const trexio_validate::Report report =
      trexio_validate::validate(file, trexio_validate::Options().require("mo_orthonormality"));
  std::fputs(report.text().c_str(), stdout);

  trexio_close(file);  // the handle stays owned by the caller
  return report.ok() ? 0 : 1;
}
