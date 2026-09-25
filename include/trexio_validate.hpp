// C++ interface of trexio-validate: a header-only wrapper of trexio_validate.h.
//
//   trexio_t* file = trexio_open("h2o", 'w', TREXIO_MEMORY, &rc);
//   ... write the data ...
//   trexio_validate::Options options;
//   options.require("mo_orthonormality");
//   const trexio_validate::Report report = trexio_validate::validate(file, options);
//   if (!report.ok()) std::cerr << report.text();
//
// The same restrictions as for the C interface apply: the file is only read and
// not closed, and it must have been opened with the TREXIO library that
// trexio-validate is linked to.
#ifndef TREXIO_VALIDATE_HPP
#define TREXIO_VALIDATE_HPP

#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#include "trexio_validate.h"

namespace trexio_validate {

enum class Status { pass = TREXIO_VALIDATE_PASS, fail = TREXIO_VALIDATE_FAIL, skip = TREXIO_VALIDATE_SKIP };

struct Result {
  std::string name;
  Status status;
  std::string detail;
};

struct CheckInfo {
  std::string name;
  std::string description;
};

inline std::vector<CheckInfo> checks() {
  std::vector<CheckInfo> out;
  for (int32_t i = 0; i < trexio_validate_check_count(); ++i) {
    out.push_back({trexio_validate_check_name(i), trexio_validate_check_description(i)});
  }
  return out;
}

// Options of a validation; the setters throw std::invalid_argument for unknown
// check names and negative values.
class Options {
 public:
  Options() : options_(trexio_validate_options_create(), trexio_validate_options_destroy) {
    if (!options_) throw std::bad_alloc();
  }

  Options& tolerance(double tol) { return check(trexio_validate_options_set_tolerance(get(), tol), "tolerance"); }
  Options& tolerance(const std::string& name, double tol) {
    return check(trexio_validate_options_set_check_tolerance(get(), name.c_str(), tol), name);
  }
  Options& select(const std::string& name) { return check(trexio_validate_options_select(get(), name.c_str()), name); }
  Options& skip(const std::string& name) { return check(trexio_validate_options_skip(get(), name.c_str()), name); }
  Options& require(const std::string& name) {
    return check(trexio_validate_options_require(get(), name.c_str()), name);
  }
  Options& max_eri_dim(int32_t dim) {
    return check(trexio_validate_options_set_max_eri_dim(get(), dim), "max_eri_dim");
  }

  trexio_validate_options_t* get() const { return options_.get(); }

 private:
  Options& check(int rc, const std::string& what) {
    if (rc != TREXIO_VALIDATE_OK) throw std::invalid_argument("trexio_validate: invalid option " + what);
    return *this;
  }

  std::shared_ptr<trexio_validate_options_t> options_;
};

class Report {
 public:
  explicit Report(trexio_validate_report_t* report) : report_(report, trexio_validate_report_destroy) {}

  // TREXIO_VALIDATE_OK, TREXIO_VALIDATE_FAILED or TREXIO_VALIDATE_NOTHING_CHECKED.
  int result() const { return trexio_validate_report_result(report_.get()); }
  // True if nothing failed. A file where nothing could be checked counts as
  // valid; use result() to tell the two apart.
  bool ok() const { return result() != TREXIO_VALIDATE_FAILED; }

  std::vector<Result> results() const {
    std::vector<Result> out;
    for (int32_t i = 0; i < trexio_validate_report_count(report_.get()); ++i) {
      out.push_back({trexio_validate_report_name(report_.get(), i),
                     static_cast<Status>(trexio_validate_report_status(report_.get(), i)),
                     trexio_validate_report_detail(report_.get(), i)});
    }
    return out;
  }

  std::string text() const {
    const char* t = trexio_validate_report_text(report_.get());
    return t != nullptr ? t : "";
  }

 private:
  std::shared_ptr<trexio_validate_report_t> report_;
};

inline Report validate(trexio_t* file, const Options& options = Options()) {
  if (file == nullptr) throw std::invalid_argument("trexio_validate: null TREXIO file");
  trexio_validate_report_t* report = nullptr;
  trexio_validate_run(file, options.get(), &report);
  if (report == nullptr) throw std::bad_alloc();
  return Report(report);
}

inline Report validate(const std::string& path, const Options& options = Options()) {
  trexio_validate_report_t* report = nullptr;
  trexio_validate_path(path.c_str(), options.get(), &report);
  if (report == nullptr) throw std::bad_alloc();
  return Report(report);
}

}  // namespace trexio_validate

#endif  // TREXIO_VALIDATE_HPP
