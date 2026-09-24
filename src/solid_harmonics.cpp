#include "solid_harmonics.hpp"

#include <cmath>
#include <cstdlib>

namespace tv {

namespace {

double factorial(int n) {
  double r = 1.0;
  for (int i = 2; i <= n; ++i) r *= i;
  return r;
}

double binomial(int n, int k) {
  if (k < 0 || k > n) return 0.0;
  return factorial(n) / (factorial(k) * factorial(n - k));
}

int cartesian_index(int a, int b, int c) {
  // Position of x^a y^b z^c in the canonical order of degree l = a+b+c.
  const int l = a + b + c;
  const int rest = l - a;  // = b + c
  return rest * (rest + 1) / 2 + c;
}

}  // namespace

std::vector<std::array<int, 3>> cartesian_exponents(int l) {
  std::vector<std::array<int, 3>> out;
  out.reserve(static_cast<size_t>(ncart(l)));
  for (int a = l; a >= 0; --a) {
    for (int b = l - a; b >= 0; --b) out.push_back({a, b, l - a - b});
  }
  return out;
}

std::vector<int> trexio_m_order(int l) {
  std::vector<int> m{0};
  for (int k = 1; k <= l; ++k) {
    m.push_back(k);
    m.push_back(-k);
  }
  return m;
}

// T. Helgaker, P. Jorgensen, J. Olsen, Molecular Electronic-Structure Theory,
// Eqs. (6.4.47)-(6.4.50). These real solid harmonics have Racah's
// normalization and positive leading coefficients, which is the convention
// fixed by the TREXIO specification.
std::vector<double> solid_harmonic(int l, int m) {
  std::vector<double> coef(static_cast<size_t>(ncart(l)), 0.0);
  const int am = std::abs(m);
  const int vm2 = m < 0 ? 1 : 0;  // 2 * v_m
  const double norm = std::sqrt(2.0 * factorial(l + am) * factorial(l - am) / (m == 0 ? 2.0 : 1.0)) /
                      (std::pow(2.0, am) * factorial(l));
  for (int t = 0; t <= (l - am) / 2; ++t) {
    for (int u = 0; u <= t; ++u) {
      // v runs over integers (m >= 0) or half-integers (m < 0); v2 = 2v.
      for (int v2 = vm2; v2 <= am; v2 += 2) {
        const int sign = ((t + (v2 - vm2) / 2) % 2 == 0) ? 1 : -1;
        const double c = sign * std::pow(0.25, t) * binomial(l, t) * binomial(l - t, am + t) *
                         binomial(t, u) * binomial(am, v2);
        const int ex = 2 * t + am - 2 * u - v2;
        const int ey = 2 * u + v2;
        const int ez = l - 2 * t - am;
        coef[static_cast<size_t>(cartesian_index(ex, ey, ez))] += norm * c;
      }
    }
  }
  return coef;
}

}  // namespace tv
