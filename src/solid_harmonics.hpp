// Real regular solid harmonics in the TREXIO convention.
#pragma once

#include <array>
#include <vector>

namespace tv {

// Number of Cartesian monomials x^a y^b z^c with a+b+c = l.
inline int ncart(int l) { return (l + 1) * (l + 2) / 2; }

// Exponents (a, b, c) of the Cartesian monomials of degree l in the canonical
// (alphabetical) order used by both TREXIO and libcint:
// xx, xy, xz, yy, yz, zz for l = 2.
std::vector<std::array<int, 3>> cartesian_exponents(int l);

// The magnetic quantum numbers of a spherical shell in the TREXIO order
// 0, +1, -1, +2, -2, ..., +l, -l.
std::vector<int> trexio_m_order(int l);

// Expansion coefficients of the real regular solid harmonic S_l^m in the
// Cartesian monomials of degree l (ordered as in cartesian_exponents), for
// Racah's normalization S_l^m = sqrt(4 pi / (2l+1)) r^l Y_l^m and the phase
// convention of the TREXIO specification (S_1^{+1} = x, S_1^{-1} = y).
std::vector<double> solid_harmonic(int l, int m);

}  // namespace tv
