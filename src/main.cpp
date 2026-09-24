// trexio-validate: check the contents of TREXIO files against integrals
// recomputed with libcint.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "checks.hpp"

namespace {

// Exit codes. 77 is the conventional "skipped" code (CTest SKIP_RETURN_CODE,
// Automake), used when nothing in the files could be checked.
constexpr int exit_ok = 0;
constexpr int exit_failed = 1;
constexpr int exit_usage = 2;
constexpr int exit_nothing_checked = 77;

void usage(std::ostream& os) {
  os << "Usage: trexio-validate [options] FILE...\n"
        "\n"
        "Validate TREXIO files by recomputing their contents with libcint.\n"
        "\n"
        "Options:\n"
        "  -t, --tolerance X      absolute tolerance of all comparisons (default 1e-8)\n"
        "      --tol CHECK=X      tolerance for one check (repeatable)\n"
        "  -c, --checks A,B,...   run only these checks\n"
        "  -s, --skip A,B,...     do not run these checks\n"
        "  -r, --require A,B,...  fail instead of skipping these checks when their data\n"
        "                         is missing; 'all' requires every check\n"
        "      --max-eri-dim N    largest ao.num/mo.num for which checks that need the\n"
        "                         full ERI tensor are run (default 64)\n"
        "  -q, --quiet            print only failures and the summary\n"
        "  -l, --list-checks      list the available checks and exit\n"
        "  -h, --help             show this help and exit\n"
        "\n"
        "Exit status: 0 if all checks that could run passed, 1 if any check failed,\n"
        "2 on usage errors, 77 if nothing could be checked.\n";
}

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (!item.empty()) out.push_back(item);
  }
  return out;
}

bool known_check(const std::string& name) {
  for (const auto& c : tv::all_checks()) {
    if (c.name == name) return true;
  }
  return false;
}

double parse_double(const std::string& s, const std::string& what) {
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (s.empty() || *end != '\0' || !(v >= 0.0)) throw std::invalid_argument("invalid " + what + ": " + s);
  return v;
}

const char* status_label(tv::Status s) {
  switch (s) {
    case tv::Status::pass: return "PASS";
    case tv::Status::fail: return "FAIL";
    case tv::Status::skip: return "SKIP";
  }
  return "?";
}

}  // namespace

int main(int argc, char** argv) {
  tv::Options opt;
  std::vector<std::string> files;
  bool quiet = false;

  try {
    std::vector<std::string> args(argv + 1, argv + argc);
    for (size_t k = 0; k < args.size(); ++k) {
      const std::string& a = args[k];
      auto value = [&]() -> std::string {
        if (k + 1 >= args.size()) throw std::invalid_argument("option " + a + " needs a value");
        return args[++k];
      };
      auto add_names = [&](std::set<std::string>& dest, const std::string& list, bool allow_all) {
        for (const auto& n : split(list)) {
          if (!known_check(n) && !(allow_all && n == "all")) throw std::invalid_argument("unknown check: " + n);
          dest.insert(n);
        }
      };
      if (a == "-h" || a == "--help") {
        usage(std::cout);
        return exit_ok;
      } else if (a == "-l" || a == "--list-checks") {
        for (const auto& c : tv::all_checks()) std::printf("%-28s %s\n", c.name.c_str(), c.description.c_str());
        return exit_ok;
      } else if (a == "-t" || a == "--tolerance") {
        opt.tolerance = parse_double(value(), "tolerance");
      } else if (a == "--tol") {
        const std::string v = value();
        const auto eq = v.find('=');
        if (eq == std::string::npos) throw std::invalid_argument("--tol expects CHECK=VALUE");
        const std::string name = v.substr(0, eq);
        if (!known_check(name)) throw std::invalid_argument("unknown check: " + name);
        opt.tolerance_override[name] = parse_double(v.substr(eq + 1), "tolerance");
      } else if (a == "-c" || a == "--checks") {
        add_names(opt.only, value(), false);
      } else if (a == "-s" || a == "--skip") {
        add_names(opt.skip, value(), false);
      } else if (a == "-r" || a == "--require") {
        add_names(opt.require, value(), true);
      } else if (a == "--max-eri-dim") {
        opt.max_dense_eri_dim = static_cast<int>(parse_double(value(), "dimension"));
      } else if (a == "-q" || a == "--quiet") {
        quiet = true;
      } else if (!a.empty() && a[0] == '-') {
        throw std::invalid_argument("unknown option: " + a);
      } else {
        files.push_back(a);
      }
    }
    if (files.empty()) throw std::invalid_argument("no input file");
  } catch (const std::invalid_argument& e) {
    std::cerr << "trexio-validate: " << e.what() << "\n\n";
    usage(std::cerr);
    return exit_usage;
  }

  int passed = 0, failed = 0, skipped = 0;
  for (const auto& path : files) {
    std::printf("%s\n", path.c_str());
    try {
      tv::TrexioFile file(path);
      for (const auto& r : tv::run_checks(file, opt)) {
        switch (r.status) {
          case tv::Status::pass: ++passed; break;
          case tv::Status::fail: ++failed; break;
          case tv::Status::skip: ++skipped; break;
        }
        if (quiet && r.status != tv::Status::fail) continue;
        std::printf("  %-4s  %-28s %s\n", status_label(r.status), r.name.c_str(), r.detail.c_str());
      }
    } catch (const tv::Error& e) {
      ++failed;
      std::printf("  FAIL  %-28s %s\n", "read", e.what());
    }
  }
  std::printf("%d passed, %d failed, %d skipped\n", passed, failed, skipped);

  if (failed > 0) return exit_failed;
  if (passed == 0) return exit_nothing_checked;
  return exit_ok;
}
