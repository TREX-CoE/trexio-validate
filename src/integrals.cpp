#include "integrals.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

extern "C" {
#include <cint_funcs.h>
}

#include "solid_harmonics.hpp"

namespace tv {

namespace {

// Highest angular momentum accepted. libcint itself goes higher, but this is
// far beyond any basis in practical use and keeps allocations bounded.
constexpr int max_ang_mom = 10;

// libcint multiplies s and p functions by the angular normalization of the
// corresponding real spherical harmonic (CINTcommon_fac_sp), also in the
// Cartesian integrals.
double libcint_common_factor(int l) {
  if (l == 0) return 0.282094791773878143;  // 1/sqrt(4 pi)
  if (l == 1) return 0.488602511902919921;  // sqrt(3/(4 pi))
  return 1.0;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

int nfunc(int l, bool cartesian) { return cartesian ? ncart(l) : 2 * l + 1; }

}  // namespace

void transform_axis(const std::vector<double>& in, std::array<int, 4>& dims, int axis,
                    const Matrix& t, std::vector<double>& out) {
  size_t stride = 1;
  for (int a = 0; a < axis; ++a) stride *= static_cast<size_t>(dims[static_cast<size_t>(a)]);
  size_t outer = 1;
  for (int a = axis + 1; a < 4; ++a) outer *= static_cast<size_t>(dims[static_cast<size_t>(a)]);
  const size_t nin = static_cast<size_t>(dims[static_cast<size_t>(axis)]);
  const size_t nout = static_cast<size_t>(t.rows);

  out.assign(stride * nout * outer, 0.0);
  for (size_t o = 0; o < outer; ++o) {
    for (size_t r = 0; r < nout; ++r) {
      double* dst = &out[stride * (r + nout * o)];
      for (size_t c = 0; c < nin; ++c) {
        const double coef = t(static_cast<int>(r), static_cast<int>(c));
        if (coef == 0.0) continue;
        const double* src = &in[stride * (c + nin * o)];
        for (size_t s = 0; s < stride; ++s) dst[s] += coef * src[s];
      }
    }
  }
  dims[static_cast<size_t>(axis)] = t.rows;
}

Basis::Basis(const TrexioData& d) {
  if (d.periodic) throw Unsupported("periodic systems are not supported");
  if (lower(d.basis_type) != "gaussian") {
    throw Unsupported("basis_type is '" + d.basis_type + "'; only Gaussian basis sets are supported");
  }
  if (d.basis_has_complex) throw Unsupported("complex basis exponents/coefficients are not supported");
  if (d.basis_has_oscillation) throw Unsupported("oscillating basis functions are not supported");
  if (!d.ao_cartesian) throw Error("ao_cartesian is missing");
  const bool cartesian = *d.ao_cartesian != 0;

  std::vector<std::string> problems;
  auto problem = [&problems](const std::string& msg) {
    if (problems.size() < 20) problems.push_back(msg);
  };

  const int nsh = d.basis_shell_num;
  const int npr = d.basis_prim_num;
  if (nsh <= 0) problem("basis_shell_num must be positive");
  for (int s = 0; s < nsh; ++s) {
    const int a = d.basis_nucleus_index[static_cast<size_t>(s)];
    const int l = d.basis_shell_ang_mom[static_cast<size_t>(s)];
    if (a < 0 || a >= d.nucleus_num) {
      problem("shell " + std::to_string(s) + ": nucleus_index " + std::to_string(a) + " out of range");
    }
    if (l < 0 || l > max_ang_mom) {
      problem("shell " + std::to_string(s) + ": unsupported angular momentum " + std::to_string(l));
    }
    if (d.basis_r_power[static_cast<size_t>(s)] != 0) {
      throw Unsupported("shell " + std::to_string(s) + " has r_power " +
                        std::to_string(d.basis_r_power[static_cast<size_t>(s)]) +
                        "; only r_power = 0 is supported for Gaussians");
    }
  }

  // Primitives of each shell; they need not be contiguous in the file.
  std::vector<std::vector<int>> shell_prims(static_cast<size_t>(std::max(nsh, 0)));
  for (int p = 0; p < npr; ++p) {
    const int s = d.basis_shell_index[static_cast<size_t>(p)];
    if (s < 0 || s >= nsh) {
      problem("primitive " + std::to_string(p) + ": shell_index " + std::to_string(s) + " out of range");
      continue;
    }
    if (!(d.basis_exponent[static_cast<size_t>(p)] > 0.0)) {
      problem("primitive " + std::to_string(p) + ": exponent must be positive");
    }
    shell_prims[static_cast<size_t>(s)].push_back(p);
  }
  for (int s = 0; s < nsh; ++s) {
    if (shell_prims[static_cast<size_t>(s)].empty()) problem("shell " + std::to_string(s) + " has no primitives");
  }

  // AOs of each shell.
  nao_ = d.ao_num;
  shell_aos_.assign(static_cast<size_t>(std::max(nsh, 0)), {});
  // ao.num must agree with the number of functions the shells define.
  int expected_nao = 0, other_nao = 0;
  for (int s = 0; s < nsh; ++s) {
    const int l = d.basis_shell_ang_mom[static_cast<size_t>(s)];
    if (l < 0 || l > max_ang_mom) continue;
    expected_nao += nfunc(l, cartesian);
    other_nao += nfunc(l, !cartesian);
  }
  if (expected_nao != nao_) {
    std::string msg = "ao.num is " + std::to_string(nao_) + " but the shells define " + std::to_string(expected_nao) +
                      (cartesian ? " Cartesian" : " spherical") + " functions";
    if (other_nao == nao_ && other_nao != expected_nao) {
      msg += std::string("; ") + std::to_string(nao_) + " is the number of " +
             (cartesian ? "spherical" : "Cartesian") + " functions, so ao.cartesian = " +
             std::to_string(*d.ao_cartesian) + " is probably wrong";
    }
    problem(msg);
  }
  if (!d.ao_shell.empty()) {
    for (int i = 0; i < nao_; ++i) {
      const int s = d.ao_shell[static_cast<size_t>(i)];
      if (s < 0 || s >= nsh) {
        problem("AO " + std::to_string(i) + ": ao_shell " + std::to_string(s) + " out of range");
        continue;
      }
      shell_aos_[static_cast<size_t>(s)].push_back(i);
    }
  } else if (expected_nao == nao_) {
    int i = 0;
    for (int s = 0; s < nsh; ++s) {
      for (int k = 0; k < nfunc(d.basis_shell_ang_mom[static_cast<size_t>(s)], cartesian); ++k) {
        shell_aos_[static_cast<size_t>(s)].push_back(i++);
      }
    }
  }
  for (int s = 0; s < nsh && problems.empty(); ++s) {
    const int l = d.basis_shell_ang_mom[static_cast<size_t>(s)];
    const int n = static_cast<int>(shell_aos_[static_cast<size_t>(s)].size());
    if (n != nfunc(l, cartesian)) {
      problem("shell " + std::to_string(s) + " (l=" + std::to_string(l) + ") has " + std::to_string(n) +
              " AOs in ao_shell, expected " + std::to_string(nfunc(l, cartesian)));
    }
  }
  if (!problems.empty()) {
    std::ostringstream os;
    for (size_t k = 0; k < problems.size(); ++k) os << (k ? "; " : "") << problems[k];
    throw Error(os.str());
  }

  // libcint data.
  env_.assign(PTR_ENV_START, 0.0);
  for (int a = 0; a < d.nucleus_num; ++a) {
    const std::array<double, 3> r = {d.nucleus_coord[static_cast<size_t>(3 * a)],
                                     d.nucleus_coord[static_cast<size_t>(3 * a + 1)],
                                     d.nucleus_coord[static_cast<size_t>(3 * a + 2)]};
    coords_.push_back(r);
    charges_.push_back(d.nucleus_charge[static_cast<size_t>(a)]);
    // The nuclear attraction is computed with int1e_rinv and the stored
    // (possibly fractional) charges, so libcint's integer charge is unused.
    atm_.insert(atm_.end(), {0, static_cast<int>(env_.size()), 0, 0, 0, 0});
    env_.insert(env_.end(), r.begin(), r.end());
  }
  for (int s = 0; s < nsh; ++s) {
    const auto& prims = shell_prims[static_cast<size_t>(s)];
    const int l = d.basis_shell_ang_mom[static_cast<size_t>(s)];
    const int ptr_exp = static_cast<int>(env_.size());
    for (int p : prims) env_.push_back(d.basis_exponent[static_cast<size_t>(p)]);
    const int ptr_coef = static_cast<int>(env_.size());
    for (int p : prims) {
      env_.push_back(d.basis_shell_factor[static_cast<size_t>(s)] * d.basis_prim_factor[static_cast<size_t>(p)] *
                     d.basis_coefficient[static_cast<size_t>(p)]);
    }
    bas_.insert(bas_.end(), {d.basis_nucleus_index[static_cast<size_t>(s)], l, static_cast<int>(prims.size()), 1, 0,
                             ptr_exp, ptr_coef, 0});

    Matrix t(nfunc(l, cartesian), ncart(l));
    if (cartesian) {
      for (int k = 0; k < t.rows; ++k) t(k, k) = 1.0;
    } else {
      const std::vector<int> ms = trexio_m_order(l);
      for (int k = 0; k < t.rows; ++k) {
        const std::vector<double> c = solid_harmonic(l, ms[static_cast<size_t>(k)]);
        for (int j = 0; j < t.cols; ++j) t(k, j) = c[static_cast<size_t>(j)];
      }
    }
    const auto& aos = shell_aos_[static_cast<size_t>(s)];
    for (int k = 0; k < t.rows; ++k) {
      const double scale = d.ao_normalization[static_cast<size_t>(aos[static_cast<size_t>(k)])] / libcint_common_factor(l);
      for (int j = 0; j < t.cols; ++j) t(k, j) *= scale;
    }
    trans_.push_back(std::move(t));
  }
}

std::vector<double> Basis::cartesian_block(int P, int Q, const std::string& op, int comp_count,
                                           std::vector<double>& env) const {
  const int lp = bas_[static_cast<size_t>(BAS_SLOTS * P + ANG_OF)];
  const int lq = bas_[static_cast<size_t>(BAS_SLOTS * Q + ANG_OF)];
  std::vector<double> buf(static_cast<size_t>(ncart(lp) * ncart(lq) * comp_count), 0.0);
  int shls[2] = {P, Q};
  int* atm = const_cast<int*>(atm_.data());
  int* bas = const_cast<int*>(bas_.data());
  const int natm = static_cast<int>(atm_.size() / ATM_SLOTS);
  const int nbas = static_cast<int>(bas_.size() / BAS_SLOTS);
  CINTIntegralFunction* fn = nullptr;
  if (op == "overlap") fn = int1e_ovlp_cart;
  else if (op == "kinetic") fn = int1e_kin_cart;
  else if (op == "rinv") fn = int1e_rinv_cart;
  else if (op == "r") fn = int1e_r_cart;
  else throw Error("internal: unknown libcint operator " + op);
  fn(buf.data(), nullptr, shls, atm, natm, bas, nbas, env.data(), nullptr, nullptr);
  return buf;
}

Matrix Basis::one_electron(const std::string& op) const {
  // Operator = sum over (libcint integral, component, prefactor, env setup).
  struct Term {
    std::string integral;
    int comp_count;
    int comp;
    double factor;
    int rinv_center;  // -1 when unused
  };
  std::vector<Term> terms;
  auto add_potential = [&]() {
    for (size_t a = 0; a < charges_.size(); ++a) {
      if (charges_[a] != 0.0) terms.push_back({"rinv", 1, 0, -charges_[a], static_cast<int>(a)});
    }
  };
  if (op == "overlap") terms.push_back({"overlap", 1, 0, 1.0, -1});
  else if (op == "kinetic") terms.push_back({"kinetic", 1, 0, 1.0, -1});
  else if (op == "potential_n_e") add_potential();
  else if (op == "core_hamiltonian") {
    terms.push_back({"kinetic", 1, 0, 1.0, -1});
    add_potential();
  }
  // The TREXIO dipole operators are mu_x = -x etc., with the origin of the
  // coordinate system as the reference point.
  else if (op == "dipole_x") terms.push_back({"r", 3, 0, -1.0, -1});
  else if (op == "dipole_y") terms.push_back({"r", 3, 1, -1.0, -1});
  else if (op == "dipole_z") terms.push_back({"r", 3, 2, -1.0, -1});
  else throw Error("internal: unknown one-electron operator " + op);

  Matrix out(nao_, nao_);
  std::vector<double> env = env_;
  std::vector<double> tmp, res;
  for (int P = 0; P < nshell(); ++P) {
    for (int Q = 0; Q < nshell(); ++Q) {
      const int np = ncart(bas_[static_cast<size_t>(BAS_SLOTS * P + ANG_OF)]);
      const int nq = ncart(bas_[static_cast<size_t>(BAS_SLOTS * Q + ANG_OF)]);
      std::vector<double> block(static_cast<size_t>(np * nq), 0.0);
      for (const Term& t : terms) {
        if (t.rinv_center >= 0) {
          const auto& r = coords_[static_cast<size_t>(t.rinv_center)];
          std::copy(r.begin(), r.end(), env.begin() + PTR_RINV_ORIG);
        }
        const std::vector<double> buf = cartesian_block(P, Q, t.integral, t.comp_count, env);
        const size_t off = static_cast<size_t>(np * nq * t.comp);
        for (size_t k = 0; k < block.size(); ++k) block[k] += t.factor * buf[off + k];
      }
      std::array<int, 4> dims = {np, nq, 1, 1};
      transform_axis(block, dims, 0, trans_[static_cast<size_t>(P)], tmp);
      transform_axis(tmp, dims, 1, trans_[static_cast<size_t>(Q)], res);
      const auto& ap = shell_aos_[static_cast<size_t>(P)];
      const auto& aq = shell_aos_[static_cast<size_t>(Q)];
      for (int j = 0; j < dims[1]; ++j) {
        for (int i = 0; i < dims[0]; ++i) {
          out(ap[static_cast<size_t>(i)], aq[static_cast<size_t>(j)]) = res[static_cast<size_t>(i + dims[0] * j)];
        }
      }
    }
  }
  return out;
}

void Basis::for_each_eri_block(
    const std::function<void(int, int, int, int, const std::vector<double>&)>& f) const {
  int* atm = const_cast<int*>(atm_.data());
  int* bas = const_cast<int*>(bas_.data());
  double* env = const_cast<double*>(env_.data());
  const int natm = static_cast<int>(atm_.size() / ATM_SLOTS);
  const int nbas = static_cast<int>(bas_.size() / BAS_SLOTS);
  CINTOpt* opt = nullptr;
  int2e_optimizer(&opt, atm, natm, bas, nbas, env);

  std::vector<double> buf, t1, t2;
  for (int P = 0; P < nbas; ++P) {
    for (int Q = 0; Q <= P; ++Q) {
      const int pq = P * (P + 1) / 2 + Q;
      for (int R = 0; R <= P; ++R) {
        for (int S = 0; S <= R; ++S) {
          if (R * (R + 1) / 2 + S > pq) break;
          int shls[4] = {P, Q, R, S};
          std::array<int, 4> dims;
          for (int k = 0; k < 4; ++k) dims[static_cast<size_t>(k)] = ncart(bas_[static_cast<size_t>(BAS_SLOTS * shls[k] + ANG_OF)]);
          buf.assign(static_cast<size_t>(dims[0]) * dims[1] * dims[2] * dims[3], 0.0);
          int2e_cart(buf.data(), nullptr, shls, atm, natm, bas, nbas, env, opt, nullptr);
          transform_axis(buf, dims, 0, trans_[static_cast<size_t>(P)], t1);
          transform_axis(t1, dims, 1, trans_[static_cast<size_t>(Q)], t2);
          transform_axis(t2, dims, 2, trans_[static_cast<size_t>(R)], t1);
          transform_axis(t1, dims, 3, trans_[static_cast<size_t>(S)], t2);
          f(P, Q, R, S, t2);
        }
      }
    }
  }
  CINTdel_optimizer(&opt);
}

std::vector<double> Basis::dense_eri() const {
  const size_t n = static_cast<size_t>(nao_);
  std::vector<double> eri(n * n * n * n, 0.0);
  auto at = [n, &eri](size_t i, size_t j, size_t k, size_t l) -> double& { return eri[((i * n + j) * n + k) * n + l]; };
  for_each_eri_block([&](int P, int Q, int R, int S, const std::vector<double>& block) {
    const auto& ap = shell_aos_[static_cast<size_t>(P)];
    const auto& aq = shell_aos_[static_cast<size_t>(Q)];
    const auto& ar = shell_aos_[static_cast<size_t>(R)];
    const auto& as = shell_aos_[static_cast<size_t>(S)];
    size_t idx = 0;
    for (int l : as)
      for (int k : ar)
        for (int j : aq)
          for (int i : ap) {
            const double v = block[idx++];
            const size_t I = static_cast<size_t>(i), J = static_cast<size_t>(j);
            const size_t K = static_cast<size_t>(k), L = static_cast<size_t>(l);
            at(I, J, K, L) = at(J, I, K, L) = at(I, J, L, K) = at(J, I, L, K) = v;
            at(K, L, I, J) = at(L, K, I, J) = at(K, L, J, I) = at(L, K, J, I) = v;
          }
  });
  return eri;
}

}  // namespace tv
