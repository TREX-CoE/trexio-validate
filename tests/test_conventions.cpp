// Unit tests of the conventions trexio-validate relies on:
//  - the real solid harmonics against the table of the TREXIO specification;
//  - the normalization and ordering of libcint's Cartesian integrals.

#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include <cint_funcs.h>
}

#include "solid_harmonics.hpp"

namespace {

int failures = 0;

void expect_near(double a, double b, double tol, const std::string& what) {
  if (!(std::abs(a - b) <= tol)) {
    std::printf("FAIL %s: %.15g != %.15g\n", what.c_str(), a, b);
    ++failures;
  }
}

using Poly = std::map<std::string, double>;  // monomial "xxy" -> coefficient

std::string monomial(const std::array<int, 3>& e) {
  return std::string(static_cast<size_t>(e[0]), 'x') + std::string(static_cast<size_t>(e[1]), 'y') +
         std::string(static_cast<size_t>(e[2]), 'z');
}

void check_harmonic(int l, int m, const Poly& expected) {
  const auto exps = tv::cartesian_exponents(l);
  const auto coef = tv::solid_harmonic(l, m);
  for (size_t k = 0; k < exps.size(); ++k) {
    const std::string mono = monomial(exps[k]);
    auto it = expected.find(mono);
    const double ref = it == expected.end() ? 0.0 : it->second;
    expect_near(coef[k], ref, 1e-14, "S(" + std::to_string(l) + "," + std::to_string(m) + ") coefficient of " + mono);
  }
}

// Table of the TREXIO specification (trex.org, "Atomic orbitals"), with
// r^2 = x^2 + y^2 + z^2 expanded.
void test_solid_harmonics() {
  const double s3 = std::sqrt(3.0), s5 = std::sqrt(5.0), s6 = std::sqrt(6.0), s10 = std::sqrt(10.0);
  const double s15 = std::sqrt(15.0), s35 = std::sqrt(35.0), s70 = std::sqrt(70.0);
  check_harmonic(0, 0, {{"", 1}});
  check_harmonic(1, 0, {{"z", 1}});
  check_harmonic(1, 1, {{"x", 1}});
  check_harmonic(1, -1, {{"y", 1}});
  check_harmonic(2, 0, {{"xx", -0.5}, {"yy", -0.5}, {"zz", 1}});
  check_harmonic(2, 1, {{"xz", s3}});
  check_harmonic(2, -1, {{"yz", s3}});
  check_harmonic(2, 2, {{"xx", s3 / 2}, {"yy", -s3 / 2}});
  check_harmonic(2, -2, {{"xy", s3}});
  // z(5z^2 - 3r^2)/2
  check_harmonic(3, 0, {{"zzz", 1}, {"xxz", -1.5}, {"yyz", -1.5}});
  // sqrt(6)/4 x(5z^2 - r^2)
  check_harmonic(3, 1, {{"xzz", s6}, {"xxx", -s6 / 4}, {"xyy", -s6 / 4}});
  check_harmonic(3, -1, {{"yzz", s6}, {"xxy", -s6 / 4}, {"yyy", -s6 / 4}});
  check_harmonic(3, 2, {{"xxz", s15 / 2}, {"yyz", -s15 / 2}});
  check_harmonic(3, -2, {{"xyz", s15}});
  check_harmonic(3, 3, {{"xxx", s10 / 4}, {"xyy", -3 * s10 / 4}});
  check_harmonic(3, -3, {{"xxy", 3 * s10 / 4}, {"yyy", -s10 / 4}});
  // (35z^4 - 30z^2 r^2 + 3r^4)/8
  check_harmonic(4, 0, {{"zzzz", 1}, {"xxzz", -3}, {"yyzz", -3}, {"xxxx", 3.0 / 8}, {"yyyy", 3.0 / 8}, {"xxyy", 6.0 / 8}});
  // sqrt(10)/4 xz(7z^2 - 3r^2)
  check_harmonic(4, 1, {{"xzzz", s10}, {"xxxz", -3 * s10 / 4}, {"xyyz", -3 * s10 / 4}});
  check_harmonic(4, -1, {{"yzzz", s10}, {"xxyz", -3 * s10 / 4}, {"yyyz", -3 * s10 / 4}});
  // sqrt(5)/4 (x^2 - y^2)(7z^2 - r^2) = sqrt(5)/4 (6x^2z^2 - 6y^2z^2 - x^4 + y^4)
  check_harmonic(4, 2, {{"xxzz", 6 * s5 / 4}, {"yyzz", -6 * s5 / 4}, {"xxxx", -s5 / 4}, {"yyyy", s5 / 4}});
  // sqrt(5)/2 xy(7z^2 - r^2)
  check_harmonic(4, -2, {{"xyzz", 3 * s5}, {"xxxy", -s5 / 2}, {"xyyy", -s5 / 2}});
  check_harmonic(4, 3, {{"xxxz", s70 / 4}, {"xyyz", -3 * s70 / 4}});
  check_harmonic(4, -3, {{"xxyz", 3 * s70 / 4}, {"yyyz", -s70 / 4}});
  check_harmonic(4, 4, {{"xxxx", s35 / 8}, {"xxyy", -6 * s35 / 8}, {"yyyy", s35 / 8}});
  check_harmonic(4, -4, {{"xxxy", s35 / 2}, {"xyyy", -s35 / 2}});

  // Racah normalization for higher l: the integral of S_l^m squared over
  // the unit sphere is 4 pi / (2l + 1). With the monomial integrals
  // int x^a y^b z^c dOmega = 2 G((a+1)/2) G((b+1)/2) G((c+1)/2) / G((a+b+c+3)/2).
  for (int l = 0; l <= 8; ++l) {
    const auto exps = tv::cartesian_exponents(l);
    for (int m : tv::trexio_m_order(l)) {
      const auto c = tv::solid_harmonic(l, m);
      double norm = 0.0;
      for (size_t i = 0; i < exps.size(); ++i)
        for (size_t j = 0; j < exps.size(); ++j) {
          int e[3];
          bool odd = false;
          for (int x = 0; x < 3; ++x) {
            e[x] = exps[i][static_cast<size_t>(x)] + exps[j][static_cast<size_t>(x)];
            odd = odd || e[x] % 2;
          }
          if (odd) continue;
          const double ang = 2.0 * std::tgamma((e[0] + 1) / 2.0) * std::tgamma((e[1] + 1) / 2.0) *
                             std::tgamma((e[2] + 1) / 2.0) / std::tgamma((e[0] + e[1] + e[2] + 3) / 2.0);
          norm += c[i] * c[j] * ang;
        }
      expect_near(norm, 4 * M_PI / (2 * l + 1), 1e-10,
                  "Racah normalization of S(" + std::to_string(l) + "," + std::to_string(m) + ")");
    }
  }
}

// Overlap of the raw monomial primitives x^a y^b z^c exp(-alpha r^2) on one
// center, computed by libcint and analytically.
void test_libcint_cartesian() {
  const double alpha = 0.7, beta = 1.3;
  for (int li = 0; li <= 4; ++li) {
    for (int lj = 0; lj <= 4; ++lj) {
      int atm[6] = {0, 20, 0, 0, 0, 0};
      std::vector<double> env(20, 0.0);
      env.insert(env.end(), {0.1, -0.2, 0.3, alpha, 1.0, beta, 1.0});
      int bas[16] = {0, li, 1, 1, 0, 23, 24, 0, 0, lj, 1, 1, 0, 25, 26, 0};
      const auto ei = tv::cartesian_exponents(li);
      const auto ej = tv::cartesian_exponents(lj);
      std::vector<double> buf(ei.size() * ej.size());
      int shls[2] = {0, 1};
      int1e_ovlp_cart(buf.data(), nullptr, shls, atm, 1, bas, 2, env.data(), nullptr, nullptr);
      // CINTcommon_fac_sp
      auto fac = [](int l) { return l == 0 ? 0.282094791773878143 : l == 1 ? 0.488602511902919921 : 1.0; };
      for (size_t i = 0; i < ei.size(); ++i)
        for (size_t j = 0; j < ej.size(); ++j) {
          double ref = 1.0;
          for (int x = 0; x < 3; ++x) {
            const int n = ei[i][static_cast<size_t>(x)] + ej[j][static_cast<size_t>(x)];
            ref *= n % 2 ? 0.0 : std::tgamma((n + 1) / 2.0) / std::pow(alpha + beta, (n + 1) / 2.0);
          }
          expect_near(buf[i + ei.size() * j] / (fac(li) * fac(lj)), ref, 1e-12 * std::max(1.0, std::abs(ref)),
                      "libcint overlap <" + monomial(ei[i]) + "|" + monomial(ej[j]) + ">");
        }
    }
  }
}

}  // namespace

int main() {
  test_solid_harmonics();
  test_libcint_cartesian();
  if (failures == 0) std::printf("all convention tests passed\n");
  return failures == 0 ? 0 : 1;
}
