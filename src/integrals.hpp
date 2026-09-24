// Recomputation of integrals over the AO basis defined in a TREXIO file.
#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "trexio_data.hpp"

namespace tv {

// Thrown when the file uses a feature trexio-validate cannot check (e.g.
// Slater-type orbitals), as opposed to a file that is inconsistent.
class Unsupported : public Error {
 public:
  using Error::Error;
};

// Dense row-major matrix.
struct Matrix {
  int rows = 0, cols = 0;
  std::vector<double> data;

  Matrix() = default;
  Matrix(int r, int c) : rows(r), cols(c), data(static_cast<size_t>(r) * static_cast<size_t>(c), 0.0) {}
  double& operator()(int i, int j) { return data[static_cast<size_t>(i) * cols + j]; }
  double operator()(int i, int j) const { return data[static_cast<size_t>(i) * cols + j]; }
};

// The AO basis of a TREXIO file, set up for libcint.
//
// libcint evaluates integrals over unnormalized Cartesian Gaussians
// x^a y^b z^c sum_k c_k exp(-alpha_k r^2) (up to a constant factor for s and p
// shells). Each TREXIO AO is a linear combination of these: for shell s with
// contraction coefficients c_k = N_s f_ks a_ks, the AO i is N'_i P_i(r) where
// P_i is either a Cartesian monomial or a real solid harmonic S_l^m. The
// per-shell matrices trans_ hold these linear combinations, so every integral
// is computed from the TREXIO definition and nothing else.
class Basis {
 public:
  // Validates the basis and AO data; throws Error if they are inconsistent
  // and Unsupported if they cannot be handled.
  explicit Basis(const TrexioData& data);

  int nao() const { return nao_; }
  int nshell() const { return static_cast<int>(shell_aos_.size()); }
  // AO indices belonging to a shell, in the order of its components.
  const std::vector<int>& shell_aos(int s) const { return shell_aos_[static_cast<size_t>(s)]; }

  // Matrix of a one-electron operator over the AOs; op is one of
  // one_electron_operators.
  Matrix one_electron(const std::string& op) const;

  // Calls f(P, Q, R, S, block) for every shell quartet with P >= Q, R >= S
  // and (P,Q) >= (R,S). block holds the chemists' integrals (ij|kl) over the
  // AOs of the four shells, with i running fastest.
  void for_each_eri_block(
      const std::function<void(int, int, int, int, const std::vector<double>&)>& f) const;

  // Full ERI tensor (ij|kl), stored as [((i*n + j)*n + k)*n + l].
  std::vector<double> dense_eri() const;

 private:
  std::vector<double> cartesian_block(int P, int Q, const std::string& op, int comp_count,
                                      std::vector<double>& env) const;

  int nao_ = 0;
  std::vector<std::vector<int>> shell_aos_;
  std::vector<Matrix> trans_;  // per shell: [AO component][Cartesian monomial]
  std::vector<int> atm_, bas_;
  std::vector<double> env_;
  std::vector<double> charges_;
  std::vector<std::array<double, 3>> coords_;
};

// Applies the matrix t along one axis of a 4-index column-major tensor.
void transform_axis(const std::vector<double>& in, std::array<int, 4>& dims, int axis,
                    const Matrix& t, std::vector<double>& out);

}  // namespace tv
