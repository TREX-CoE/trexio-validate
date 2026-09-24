// The individual validation checks.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "trexio_data.hpp"

namespace tv {

enum class Status { pass, fail, skip };

struct CheckResult {
  std::string name;
  Status status = Status::skip;
  std::string detail;
};

struct CheckInfo {
  std::string name;
  std::string description;
};

struct Options {
  double tolerance = 1e-8;
  std::map<std::string, double> tolerance_override;  // per check
  std::set<std::string> only;                        // empty: all checks
  std::set<std::string> skip;
  std::set<std::string> require;  // may contain "all"
  // Checks needing the full ERI tensor are skipped above this dimension.
  int max_dense_eri_dim = 64;
};

// All checks, in the order they are run.
const std::vector<CheckInfo>& all_checks();

std::vector<CheckResult> run_checks(const TrexioFile& file, const Options& options);

}  // namespace tv
