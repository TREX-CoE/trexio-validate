// trexio-validate: check the contents of TREXIO files against integrals
// recomputed with libcint.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "trexio_validate.h"

namespace {

// Exit codes. 77 is the conventional "skipped" code (CTest SKIP_RETURN_CODE,
// Automake), used when nothing in the files could be checked.
constexpr int exit_ok = TREXIO_VALIDATE_OK;
constexpr int exit_failed = TREXIO_VALIDATE_FAILED;
constexpr int exit_usage = TREXIO_VALIDATE_INVALID_ARGUMENT;
constexpr int exit_nothing_checked = TREXIO_VALIDATE_NOTHING_CHECKED;

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
        "  -V, --version          print the version and exit\n"
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

double parse_double(const std::string& s, const std::string& what) {
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (s.empty() || *end != '\0' || !(v >= 0.0)) throw std::invalid_argument("invalid " + what + ": " + s);
  return v;
}

}  // namespace

int main(int argc, char** argv) {
  std::unique_ptr<trexio_validate_options_t, void (*)(trexio_validate_options_t*)> options(
      trexio_validate_options_create(), trexio_validate_options_destroy);
  trexio_validate_options_t* opt = options.get();
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
      auto add_names = [](int (*add)(trexio_validate_options_t*, const char*), trexio_validate_options_t* o,
                          const std::string& list) {
        for (const auto& n : split(list)) {
          if (add(o, n.c_str()) != TREXIO_VALIDATE_OK) throw std::invalid_argument("unknown check: " + n);
        }
      };
      if (a == "-h" || a == "--help") {
        usage(std::cout);
        return exit_ok;
      } else if (a == "-l" || a == "--list-checks") {
        for (int32_t i = 0; i < trexio_validate_check_count(); ++i) {
          std::printf("%-28s %s\n", trexio_validate_check_name(i), trexio_validate_check_description(i));
        }
        return exit_ok;
      } else if (a == "-V" || a == "--version") {
        std::printf("trexio-validate %s\n", trexio_validate_version());
        return exit_ok;
      } else if (a == "-t" || a == "--tolerance") {
        trexio_validate_options_set_tolerance(opt, parse_double(value(), "tolerance"));
      } else if (a == "--tol") {
        const std::string v = value();
        const auto eq = v.find('=');
        if (eq == std::string::npos) throw std::invalid_argument("--tol expects CHECK=VALUE");
        const std::string name = v.substr(0, eq);
        const double tol = parse_double(v.substr(eq + 1), "tolerance");
        if (trexio_validate_options_set_check_tolerance(opt, name.c_str(), tol) != TREXIO_VALIDATE_OK) {
          throw std::invalid_argument("unknown check: " + name);
        }
      } else if (a == "-c" || a == "--checks") {
        add_names(trexio_validate_options_select, opt, value());
      } else if (a == "-s" || a == "--skip") {
        add_names(trexio_validate_options_skip, opt, value());
      } else if (a == "-r" || a == "--require") {
        add_names(trexio_validate_options_require, opt, value());
      } else if (a == "--max-eri-dim") {
        trexio_validate_options_set_max_eri_dim(opt, static_cast<int32_t>(parse_double(value(), "dimension")));
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
    trexio_validate_report_t* report = nullptr;
    trexio_validate_path(path.c_str(), opt, &report);
    if (report == nullptr) {
      ++failed;
      std::printf("  FAIL  %-28s %s\n", "read", "out of memory");
      continue;
    }
    for (int32_t i = 0; i < trexio_validate_report_count(report); ++i) {
      const trexio_validate_status_t status = trexio_validate_report_status(report, i);
      passed += status == TREXIO_VALIDATE_PASS;
      failed += status == TREXIO_VALIDATE_FAIL;
      skipped += status == TREXIO_VALIDATE_SKIP;
    }
    if (!quiet) {
      std::fputs(trexio_validate_report_text(report), stdout);
    } else {
      for (int32_t i = 0; i < trexio_validate_report_count(report); ++i) {
        if (trexio_validate_report_status(report, i) != TREXIO_VALIDATE_FAIL) continue;
        std::printf("  FAIL  %-28s %s\n", trexio_validate_report_name(report, i),
                    trexio_validate_report_detail(report, i));
      }
    }
    trexio_validate_report_destroy(report);
  }
  std::printf("%d passed, %d failed, %d skipped\n", passed, failed, skipped);

  if (failed > 0) return exit_failed;
  if (passed == 0) return exit_nothing_checked;
  return exit_ok;
}
