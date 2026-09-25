// Validation of TREXIO_MEMORY files through the C++ interface.

#include <cstdio>
#include <string>

#include "memory_helper.h"
#include "trexio_validate.hpp"

namespace {

int failures = 0;

void expect(bool cond, const std::string& what) {
  if (!cond) {
    std::printf("FAIL %s\n", what.c_str());
    ++failures;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::printf("usage: test_memory FILE\n");
    return 2;
  }
  const std::string path = argv[1];
  if (!tv_memory_available()) {
    std::printf("TREXIO has no in-memory back end: skipped\n");
    return 77;
  }

  // An intact copy passes everything the original passes.
  trexio_t* file = tv_memory_copy(path.c_str(), TV_MEMORY_INTACT);
  expect(file != nullptr, "copy into TREXIO_MEMORY");
  if (file == nullptr) return 1;
  const int32_t natoms = tv_memory_nucleus_num(file);
  const trexio_validate::Report report = trexio_validate::validate(file, trexio_validate::Options().require("all"));
  std::fputs(report.text().c_str(), stdout);
  expect(report.result() == TREXIO_VALIDATE_OK, "intact memory file validates");
  expect(report.results().size() == trexio_validate::checks().size(), "every check ran");
  // The handle is left open and usable.
  expect(tv_memory_nucleus_num(file) == natoms && natoms > 0, "file still usable after validation");
  tv_memory_close(file);

  // Corrupted copies fail the check that catches them.
  struct Case {
    int corruption;
    const char* check;
  };
  for (const Case c : {Case{TV_MEMORY_BAD_NUCLEUS_REPULSION, "nucleus_repulsion"},
                       Case{TV_MEMORY_BAD_MO_COEFFICIENT, "mo_orthonormality"}}) {
    trexio_t* bad = tv_memory_copy(path.c_str(), c.corruption);
    expect(bad != nullptr, "corrupted copy");
    if (bad == nullptr) continue;
    const trexio_validate::Report r = trexio_validate::validate(bad);
    expect(!r.ok(), std::string("corrupted file fails: ") + c.check);
    for (const auto& result : r.results()) {
      if (result.name == c.check) expect(result.status == trexio_validate::Status::fail, c.check);
    }
    tv_memory_close(bad);
  }

  // Unknown check names are rejected.
  bool threw = false;
  try {
    trexio_validate::Options().select("no_such_check");
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "unknown check name throws");

  if (failures == 0) std::printf("memory back-end tests passed\n");
  return failures == 0 ? 0 : 1;
}
