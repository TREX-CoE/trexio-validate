// The C interface declared in trexio_validate.h.

#include "trexio_validate.h"

#include <cstdio>
#include <exception>
#include <new>
#include <string>
#include <vector>

#include "checks.hpp"

struct trexio_validate_options_s {
  tv::Options options;
};

struct trexio_validate_report_s {
  std::vector<tv::CheckResult> results;
  int result = TREXIO_VALIDATE_NOTHING_CHECKED;
  std::string text;
};

namespace {

bool known_check(const char* name) {
  if (name == nullptr) return false;
  for (const auto& c : tv::all_checks()) {
    if (c.name == name) return true;
  }
  return false;
}

const char* status_label(tv::Status s) {
  switch (s) {
    case tv::Status::pass: return "PASS";
    case tv::Status::fail: return "FAIL";
    case tv::Status::skip: return "SKIP";
  }
  return "?";
}

void finish(trexio_validate_report_s& r) {
  int passed = 0, failed = 0;
  for (const auto& c : r.results) {
    passed += c.status == tv::Status::pass;
    failed += c.status == tv::Status::fail;
    char prefix[64];
    std::snprintf(prefix, sizeof(prefix), "  %-4s  %-28s ", status_label(c.status), c.name.c_str());
    r.text += prefix + c.detail + "\n";
  }
  r.result = failed > 0 ? TREXIO_VALIDATE_FAILED : passed > 0 ? TREXIO_VALIDATE_OK : TREXIO_VALIDATE_NOTHING_CHECKED;
}

// Runs the checks, turning any error into a failed "read" entry.
template <class Open>
int run(Open open, const trexio_validate_options_t* options, trexio_validate_report_t** report) {
  if (report != nullptr) *report = nullptr;
  trexio_validate_report_s* r = new (std::nothrow) trexio_validate_report_s;
  if (r == nullptr) return TREXIO_VALIDATE_FAILED;
  try {
    static const tv::Options defaults;
    tv::TrexioFile file = open();
    r->results = tv::run_checks(file, options != nullptr ? options->options : defaults);
  } catch (const std::exception& e) {
    r->results.push_back({"read", tv::Status::fail, e.what()});
  } catch (...) {
    r->results.push_back({"read", tv::Status::fail, "unknown error"});
  }
  finish(*r);
  const int result = r->result;
  if (report != nullptr) {
    *report = r;
  } else {
    delete r;
  }
  return result;
}

}  // namespace

extern "C" {

const char* trexio_validate_version(void) { return TREXIO_VALIDATE_VERSION; }

int32_t trexio_validate_check_count(void) { return static_cast<int32_t>(tv::all_checks().size()); }

const char* trexio_validate_check_name(int32_t i) {
  if (i < 0 || i >= trexio_validate_check_count()) return nullptr;
  return tv::all_checks()[static_cast<size_t>(i)].name.c_str();
}

const char* trexio_validate_check_description(int32_t i) {
  if (i < 0 || i >= trexio_validate_check_count()) return nullptr;
  return tv::all_checks()[static_cast<size_t>(i)].description.c_str();
}

trexio_validate_options_t* trexio_validate_options_create(void) {
  return new (std::nothrow) trexio_validate_options_s;
}

void trexio_validate_options_destroy(trexio_validate_options_t* options) { delete options; }

int trexio_validate_options_set_tolerance(trexio_validate_options_t* options, double tolerance) {
  if (options == nullptr || !(tolerance >= 0.0)) return TREXIO_VALIDATE_INVALID_ARGUMENT;
  options->options.tolerance = tolerance;
  return TREXIO_VALIDATE_OK;
}

int trexio_validate_options_set_check_tolerance(trexio_validate_options_t* options, const char* check,
                                                double tolerance) {
  if (options == nullptr || !known_check(check) || !(tolerance >= 0.0)) return TREXIO_VALIDATE_INVALID_ARGUMENT;
  options->options.tolerance_override[check] = tolerance;
  return TREXIO_VALIDATE_OK;
}

int trexio_validate_options_select(trexio_validate_options_t* options, const char* check) {
  if (options == nullptr || !known_check(check)) return TREXIO_VALIDATE_INVALID_ARGUMENT;
  options->options.only.insert(check);
  return TREXIO_VALIDATE_OK;
}

int trexio_validate_options_skip(trexio_validate_options_t* options, const char* check) {
  if (options == nullptr || !known_check(check)) return TREXIO_VALIDATE_INVALID_ARGUMENT;
  options->options.skip.insert(check);
  return TREXIO_VALIDATE_OK;
}

int trexio_validate_options_require(trexio_validate_options_t* options, const char* check) {
  if (options == nullptr || check == nullptr || !(known_check(check) || std::string(check) == "all")) {
    return TREXIO_VALIDATE_INVALID_ARGUMENT;
  }
  options->options.require.insert(check);
  return TREXIO_VALIDATE_OK;
}

int trexio_validate_options_set_max_eri_dim(trexio_validate_options_t* options, int32_t dim) {
  if (options == nullptr || dim < 0) return TREXIO_VALIDATE_INVALID_ARGUMENT;
  options->options.max_dense_eri_dim = dim;
  return TREXIO_VALIDATE_OK;
}

int32_t trexio_validate_report_count(const trexio_validate_report_t* report) {
  return report == nullptr ? 0 : static_cast<int32_t>(report->results.size());
}

const char* trexio_validate_report_name(const trexio_validate_report_t* report, int32_t i) {
  if (i < 0 || i >= trexio_validate_report_count(report)) return nullptr;
  return report->results[static_cast<size_t>(i)].name.c_str();
}

trexio_validate_status_t trexio_validate_report_status(const trexio_validate_report_t* report, int32_t i) {
  if (i < 0 || i >= trexio_validate_report_count(report)) return TREXIO_VALIDATE_SKIP;
  switch (report->results[static_cast<size_t>(i)].status) {
    case tv::Status::pass: return TREXIO_VALIDATE_PASS;
    case tv::Status::fail: return TREXIO_VALIDATE_FAIL;
    case tv::Status::skip: return TREXIO_VALIDATE_SKIP;
  }
  return TREXIO_VALIDATE_SKIP;
}

const char* trexio_validate_report_detail(const trexio_validate_report_t* report, int32_t i) {
  if (i < 0 || i >= trexio_validate_report_count(report)) return nullptr;
  return report->results[static_cast<size_t>(i)].detail.c_str();
}

int trexio_validate_report_result(const trexio_validate_report_t* report) {
  return report == nullptr ? TREXIO_VALIDATE_INVALID_ARGUMENT : report->result;
}

const char* trexio_validate_report_text(const trexio_validate_report_t* report) {
  return report == nullptr ? nullptr : report->text.c_str();
}

void trexio_validate_report_destroy(trexio_validate_report_t* report) { delete report; }

int trexio_validate_run(trexio_t* file, const trexio_validate_options_t* options, trexio_validate_report_t** report) {
  if (report != nullptr) *report = nullptr;
  if (file == nullptr) return TREXIO_VALIDATE_INVALID_ARGUMENT;
  return run([file] { return tv::TrexioFile(file, "<open TREXIO file>"); }, options, report);
}

int trexio_validate_path(const char* path, const trexio_validate_options_t* options,
                         trexio_validate_report_t** report) {
  if (report != nullptr) *report = nullptr;
  if (path == nullptr) return TREXIO_VALIDATE_INVALID_ARGUMENT;
  return run([path] { return tv::TrexioFile(path); }, options, report);
}

}  // extern "C"
