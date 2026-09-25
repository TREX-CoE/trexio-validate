#include "checks.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <unordered_map>

#include "integrals.hpp"

namespace tv {

const std::vector<CheckInfo>& all_checks() {
  static const std::vector<CheckInfo> checks = [] {
    std::vector<CheckInfo> c = {
        {"basis", "basis and AO data are consistent and supported"},
        {"nucleus_repulsion", "nucleus.repulsion matches the nuclear charges and coordinates"},
        {"electron_count", "electron numbers agree with each other and with mo.occupation"},
        {"mo_orthonormality", "the MOs are orthonormal in the metric of the AO basis"},
    };
    for (const auto& op : one_electron_operators) {
      c.push_back({"ao_1e_int_" + op, "ao_1e_int." + op + " matches the recomputed AO integrals"});
    }
    for (const auto& op : one_electron_operators) {
      c.push_back({"mo_1e_int_" + op, "mo_1e_int." + op + " matches the recomputed MO integrals"});
    }
    c.push_back({"ao_2e_int_eri", "ao_2e_int.eri matches the recomputed AO electron repulsion integrals"});
    c.push_back({"mo_2e_int_eri", "mo_2e_int.eri matches the recomputed MO electron repulsion integrals"});
    for (auto& info : c) {
      auto it = unavailable_checks().find(info.name);
      if (it != unavailable_checks().end()) info.description += " [unavailable: " + it->second + "]";
    }
    return c;
  }();
  return checks;
}

namespace {

std::string fmt(const char* format, double v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), format, v);
  return buf;
}

std::string sci(double v) { return fmt("%.2e", v); }

// Largest deviation between two quantities, with the position where it occurs.
struct Deviation {
  double max = 0.0;
  long i = -1, j = -1;
  double stored = 0.0, computed = 0.0;

  void update(double s, double c, long ii, long jj = -1) {
    const double d = std::abs(s - c);
    // A NaN deviation is kept, so that it fails the comparison.
    if (i < 0 || d > max || (std::isnan(d) && !std::isnan(max))) {
      max = d;
      i = ii;
      j = jj;
      stored = s;
      computed = c;
    }
  }
};

class Runner {
 public:
  Runner(const TrexioFile& file, const Options& options) : file_(file), opt_(options) {}

  std::vector<CheckResult> run();

 private:
  // Helpers shared by the checks.
  double tol(const std::string& check) const {
    auto it = opt_.tolerance_override.find(check);
    return it == opt_.tolerance_override.end() ? opt_.tolerance : it->second;
  }
  bool required(const std::string& check) const {
    return opt_.require.count("all") > 0 || opt_.require.count(check) > 0;
  }
  // Result for a check whose input data is not available.
  CheckResult absent(const std::string& check, const std::string& why) const {
    return {check, required(check) ? Status::fail : Status::skip, why};
  }
  CheckResult compare(const std::string& check, const Deviation& dev, const std::string& what,
                      const std::string& extra = "") const;
  std::string ao_label(long i) const;
  bool spin_mismatch(long i, long j) const {
    if (!data_.mo_spin.empty() && data_.mo_spin[static_cast<size_t>(i)] != data_.mo_spin[static_cast<size_t>(j)]) {
      return true;
    }
    return !data_.mo_k_point.empty() &&
           data_.mo_k_point[static_cast<size_t>(i)] != data_.mo_k_point[static_cast<size_t>(j)];
  }
  const Matrix& ao_matrix(const std::string& op);
  std::string basis_unavailable() const {
    return basis_error_.empty() ? "" : "basis not usable: " + basis_error_;
  }

  CheckResult check_basis();
  CheckResult check_nucleus_repulsion();
  CheckResult check_electron_count();
  CheckResult check_mo_orthonormality();
  CheckResult check_ao_1e(const std::string& op);
  CheckResult check_mo_1e(const std::string& op);
  CheckResult check_ao_eri();
  CheckResult check_mo_eri();

  const TrexioFile& file_;
  const Options& opt_;
  TrexioData data_;
  std::unique_ptr<Basis> basis_;
  std::string basis_error_;
  bool basis_unsupported_ = false;
  std::map<std::string, Matrix> ao_matrices_;
};

std::vector<CheckResult> Runner::run() {
  data_ = read_trexio_data(file_);
  try {
    basis_ = std::make_unique<Basis>(data_);
  } catch (const Unsupported& e) {
    basis_error_ = e.what();
    basis_unsupported_ = true;
  } catch (const Error& e) {
    basis_error_ = e.what();
  }

  std::vector<CheckResult> results;
  for (const auto& info : all_checks()) {
    const std::string& name = info.name;
    if (!opt_.only.empty() && opt_.only.count(name) == 0) continue;
    if (opt_.skip.count(name) > 0) continue;

    CheckResult r;
    auto unavailable = unavailable_checks().find(name);
    if (unavailable != unavailable_checks().end()) {
      // Requiring such a check by name fails; "all" means all available checks.
      const bool named = opt_.require.count(name) > 0;
      results.push_back({name, named ? Status::fail : Status::skip, unavailable->second});
      continue;
    }
    try {
      if (name == "basis") r = check_basis();
      else if (name == "nucleus_repulsion") r = check_nucleus_repulsion();
      else if (name == "electron_count") r = check_electron_count();
      else if (name == "mo_orthonormality") r = check_mo_orthonormality();
      else if (name.rfind("ao_1e_int_", 0) == 0) r = check_ao_1e(name.substr(10));
      else if (name.rfind("mo_1e_int_", 0) == 0) r = check_mo_1e(name.substr(10));
      else if (name == "ao_2e_int_eri") r = check_ao_eri();
      else if (name == "mo_2e_int_eri") r = check_mo_eri();
    } catch (const Error& e) {
      r = {name, Status::fail, e.what()};
    }
    r.name = name;
    results.push_back(std::move(r));
  }
  return results;
}

CheckResult Runner::compare(const std::string& check, const Deviation& dev, const std::string& what,
                            const std::string& extra) const {
  const double t = tol(check);
  const bool ok = dev.max <= t;  // false for NaN
  std::string detail = "max |" + what + "| = " + sci(dev.max) + " (tol " + sci(t) + ")";
  if (!ok && dev.i >= 0) {
    detail += "; worst at [" + std::to_string(dev.i) + (dev.j >= 0 ? "," + std::to_string(dev.j) : "") +
              "]: found " + fmt("%.10g", dev.stored) + ", expected " + fmt("%.10g", dev.computed);
  }
  if (!extra.empty()) detail += "; " + extra;
  return {check, ok ? Status::pass : Status::fail, detail};
}

std::string Runner::ao_label(long i) const {
  if (!basis_) return "AO " + std::to_string(i);
  for (int s = 0; s < basis_->nshell(); ++s) {
    const auto& aos = basis_->shell_aos(s);
    auto it = std::find(aos.begin(), aos.end(), static_cast<int>(i));
    if (it != aos.end()) {
      return "AO " + std::to_string(i) + " (shell " + std::to_string(s) + ", l=" +
             std::to_string(data_.basis_shell_ang_mom[static_cast<size_t>(s)]) + ", component " +
             std::to_string(it - aos.begin()) + ", nucleus " +
             std::to_string(data_.basis_nucleus_index[static_cast<size_t>(s)]) + ")";
    }
  }
  return "AO " + std::to_string(i);
}

const Matrix& Runner::ao_matrix(const std::string& op) {
  auto it = ao_matrices_.find(op);
  if (it == ao_matrices_.end()) it = ao_matrices_.emplace(op, basis_->one_electron(op)).first;
  return it->second;
}

// ---------------------------------------------------------------- basis

CheckResult Runner::check_basis() {
  std::string warn;
  for (const auto& w : data_.warnings) warn += "; warning: " + w;
  const std::vector<std::string> unreadable = unreadable_fields();
  if (!unreadable.empty()) {
    warn += "; note: this TREXIO library cannot read";
    for (size_t k = 0; k < unreadable.size(); ++k) warn += (k ? ", " : " ") + unreadable[k];
  }
  if (basis_unsupported_) return {"basis", Status::skip, "unsupported: " + basis_error_ + warn};
  if (!basis_) return {"basis", Status::fail, basis_error_ + warn};
  return {"basis", Status::pass,
          std::to_string(data_.ao_num) + " " + (*data_.ao_cartesian ? "Cartesian" : "spherical") + " AOs in " +
              std::to_string(data_.basis_shell_num) + " shells" + warn};
}

// ---------------------------------------------------------------- nucleus

CheckResult Runner::check_nucleus_repulsion() {
  const std::string name = "nucleus_repulsion";
  if (!data_.nucleus_repulsion) return absent(name, "nucleus.repulsion not present");
  double e = 0.0;
  for (int a = 0; a < data_.nucleus_num; ++a) {
    for (int b = 0; b < a; ++b) {
      const double za = data_.nucleus_charge[static_cast<size_t>(a)];
      const double zb = data_.nucleus_charge[static_cast<size_t>(b)];
      if (za == 0.0 || zb == 0.0) continue;
      double r2 = 0.0;
      for (int x = 0; x < 3; ++x) {
        const double d = data_.nucleus_coord[static_cast<size_t>(3 * a + x)] -
                         data_.nucleus_coord[static_cast<size_t>(3 * b + x)];
        r2 += d * d;
      }
      if (r2 == 0.0) {
        return {name, Status::fail,
                "charged nuclei " + std::to_string(a) + " and " + std::to_string(b) + " coincide"};
      }
      e += za * zb / std::sqrt(r2);
    }
  }
  Deviation dev;
  dev.update(*data_.nucleus_repulsion, e, -1);
  CheckResult r = compare(name, dev, "E_file - E");
  r.detail += "; file " + fmt("%.12f", *data_.nucleus_repulsion) + ", computed " + fmt("%.12f", e);
  if (r.status == Status::fail) {
    r.detail += " (coordinates must be in bohr)";
  }
  return r;
}

// ---------------------------------------------------------------- electrons

CheckResult Runner::check_electron_count() {
  const std::string name = "electron_count";
  const auto& d = data_;
  if (!d.electron_num && !(d.electron_up_num && d.electron_dn_num)) {
    return absent(name, "electron.num / electron.up_num / electron.dn_num not present");
  }
  const double t = tol(name);
  std::vector<std::string> problems, done;
  if (d.electron_num && d.electron_up_num && d.electron_dn_num) {
    done.push_back("num = up_num + dn_num");
    if (*d.electron_up_num + *d.electron_dn_num != *d.electron_num) {
      problems.push_back("electron.num = " + std::to_string(*d.electron_num) +
                         " but up_num + dn_num = " + std::to_string(*d.electron_up_num + *d.electron_dn_num));
    }
  }
  if (!d.mo_occupation.empty()) {
    double total = 0.0, up = 0.0, dn = 0.0;
    bool spin_resolved = false;
    for (size_t i = 0; i < d.mo_occupation.size(); ++i) {
      total += d.mo_occupation[i];
      if (!d.mo_spin.empty()) {
        (d.mo_spin[i] == 0 ? up : dn) += d.mo_occupation[i];
        spin_resolved = spin_resolved || d.mo_spin[i] != 0;
      }
    }
    const double n = d.electron_num ? *d.electron_num : *d.electron_up_num + *d.electron_dn_num;
    done.push_back("sum of mo.occupation");
    if (std::abs(total - n) > t) {
      problems.push_back("sum of mo.occupation is " + fmt("%.10g", total) + ", expected " + fmt("%.10g", n));
    }
    if (spin_resolved && d.electron_up_num && d.electron_dn_num) {
      done.push_back("occupations per spin");
      if (std::abs(up - *d.electron_up_num) > t || std::abs(dn - *d.electron_dn_num) > t) {
        problems.push_back("occupations per spin are " + fmt("%.10g", up) + " / " + fmt("%.10g", dn) +
                           ", expected " + std::to_string(*d.electron_up_num) + " / " +
                           std::to_string(*d.electron_dn_num));
      }
    }
  }
  if (done.empty()) return absent(name, "nothing to compare");
  std::string detail;
  for (size_t k = 0; k < (problems.empty() ? done : problems).size(); ++k) {
    detail += (k ? "; " : "") + (problems.empty() ? done : problems)[k];
  }
  return {name, problems.empty() ? Status::pass : Status::fail, (problems.empty() ? "checked " : "") + detail};
}

// ---------------------------------------------------------------- MOs

CheckResult Runner::check_mo_orthonormality() {
  const std::string name = "mo_orthonormality";
  if (data_.mo_coefficient.empty()) return absent(name, "mo.coefficient not present");
  if (!basis_) return absent(name, basis_unavailable());

  const Matrix& s = ao_matrix("overlap");
  const int n = data_.ao_num, m = data_.mo_num;
  const bool cplx = !data_.mo_coefficient_im.empty();
  auto cr = [&](int i, int mu) { return data_.mo_coefficient[static_cast<size_t>(i) * n + mu]; };
  auto ci = [&](int i, int mu) { return cplx ? data_.mo_coefficient_im[static_cast<size_t>(i) * n + mu] : 0.0; };

  // SC = S C^T, then O = C^* S C^T.
  Matrix scr(n, m), sci_(n, m);
  for (int mu = 0; mu < n; ++mu)
    for (int nu = 0; nu < n; ++nu) {
      const double smn = s(mu, nu);
      if (smn == 0.0) continue;
      for (int j = 0; j < m; ++j) {
        scr(mu, j) += smn * cr(j, nu);
        if (cplx) sci_(mu, j) += smn * ci(j, nu);
      }
    }
  Deviation diag, offdiag;
  long skipped = 0;
  for (int i = 0; i < m; ++i)
    for (int j = 0; j < m; ++j) {
      if (spin_mismatch(i, j)) {
        ++skipped;
        continue;
      }
      double re = 0.0, im = 0.0;
      for (int mu = 0; mu < n; ++mu) {
        re += cr(i, mu) * scr(mu, j) + ci(i, mu) * sci_(mu, j);
        im += cr(i, mu) * sci_(mu, j) - ci(i, mu) * scr(mu, j);
      }
      const double target = i == j ? 1.0 : 0.0;
      const double mag = std::hypot(re - target, im);
      (i == j ? diag : offdiag).update(target + mag, target, i, j);
    }

  Deviation total = diag.max >= offdiag.max ? diag : offdiag;
  std::string extra = "diagonal " + sci(diag.max) + ", off-diagonal " + sci(offdiag.max);
  if (skipped > 0) extra += "; " + std::to_string(skipped) + " pairs of different spin/k-point not checked";
  CheckResult r = compare(name, total, "C^+ S C - 1", extra);
  if (r.status == Status::fail) {
    for (const auto& field : unreadable_fields()) {
      if (field == "mo.spin") {
        r.detail += "; this TREXIO library cannot read mo.spin, so the MOs of an open-shell file are "
                    "compared across spins";
      }
    }
    if (offdiag.max <= tol(name)) {
      r.detail += "; the MOs are orthogonal but not normalized, which points to a mismatch in the "
                  "normalization factors (basis.shell_factor, basis.prim_factor, ao.normalization)";
    }
    auto it = data_.ao_1e_int.find("overlap");
    if (it != data_.ao_1e_int.end() && !cplx) {
      // Are the MOs orthonormal with the overlap matrix stored in the file?
      double worst = 0.0;
      for (int i = 0; i < m; ++i)
        for (int j = 0; j < m; ++j) {
          if (spin_mismatch(i, j)) continue;
          double o = 0.0;
          for (int mu = 0; mu < n; ++mu)
            for (int nu = 0; nu < n; ++nu) o += cr(i, mu) * it->second[static_cast<size_t>(mu) * n + nu] * cr(j, nu);
          worst = std::max(worst, std::abs(o - (i == j ? 1.0 : 0.0)));
        }
      if (worst <= tol(name)) {
        r.detail += "; the MOs ARE orthonormal with the file's ao_1e_int.overlap, so the basis set as "
                    "described in the file differs from the one the program used (check AO ordering, "
                    "normalization factors, and the solid-harmonic convention)";
      }
    }
  }
  return r;
}

// ---------------------------------------------------------------- 1e integrals

CheckResult Runner::check_ao_1e(const std::string& op) {
  const std::string name = "ao_1e_int_" + op;
  auto it = data_.ao_1e_int.find(op);
  if (it == data_.ao_1e_int.end()) return absent(name, "ao_1e_int." + op + " not present");
  if (!basis_) return absent(name, basis_unavailable());
  if (data_.has_ecp && op == "core_hamiltonian") {
    return absent(name, "the core Hamiltonian contains ECP terms, which are not supported");
  }

  const Matrix& ref = ao_matrix(op);
  const std::vector<double>& file = it->second;
  const int n = data_.ao_num;
  Deviation dev, flipped;
  for (int p = 0; p < n; ++p)
    for (int q = 0; q < n; ++q) {
      const double v = file[static_cast<size_t>(p) * n + q];
      dev.update(v, ref(p, q), p, q);
      flipped.update(v, -ref(p, q), p, q);
    }
  CheckResult r = compare(name, dev, "file - computed");
  if (r.status == Status::fail) {
    r.detail += "; " + ao_label(dev.i) + ", " + ao_label(dev.j);
    if (flipped.max <= tol(name)) r.detail += "; the file matches the computed matrix with the opposite sign";
  }
  return r;
}

CheckResult Runner::check_mo_1e(const std::string& op) {
  const std::string name = "mo_1e_int_" + op;
  auto it = data_.mo_1e_int.find(op);
  if (it == data_.mo_1e_int.end()) return absent(name, "mo_1e_int." + op + " not present");
  if (data_.mo_coefficient.empty()) return absent(name, "mo.coefficient not present");
  if (!data_.mo_coefficient_im.empty()) return absent(name, "complex MO coefficients are not supported");
  if (!basis_) return absent(name, basis_unavailable());
  if (data_.has_ecp && op == "core_hamiltonian") {
    return absent(name, "the core Hamiltonian contains ECP terms, which are not supported");
  }

  const Matrix& x = ao_matrix(op);
  const int n = data_.ao_num, m = data_.mo_num;
  const double* c = data_.mo_coefficient.data();
  Matrix xc(n, m);  // X C^T
  for (int mu = 0; mu < n; ++mu)
    for (int nu = 0; nu < n; ++nu) {
      const double v = x(mu, nu);
      if (v == 0.0) continue;
      for (int j = 0; j < m; ++j) xc(mu, j) += v * c[static_cast<size_t>(j) * n + nu];
    }
  Deviation dev;
  const std::vector<double>& file = it->second;
  for (int i = 0; i < m; ++i)
    for (int j = 0; j < m; ++j) {
      if (spin_mismatch(i, j)) continue;
      double v = 0.0;
      for (int mu = 0; mu < n; ++mu) v += c[static_cast<size_t>(i) * n + mu] * xc(mu, j);
      dev.update(file[static_cast<size_t>(i) * m + j], v, i, j);
    }
  return compare(name, dev, "file - computed");
}

// ---------------------------------------------------------------- 2e integrals

// Index of the permutational-symmetry class of the chemists' integral (ab|cd)
// for real orbitals.
uint64_t eri_key(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  if (a < b) std::swap(a, b);
  if (c < d) std::swap(c, d);
  uint64_t ab = a * (a + 1) / 2 + b;
  uint64_t cd = c * (c + 1) / 2 + d;
  if (ab < cd) std::swap(ab, cd);
  return ab * (ab + 1) / 2 + cd;
}

struct StoredEri {
  std::unordered_map<uint64_t, double> values;
  Deviation duplicates;  // disagreement between symmetry-equivalent entries
  long ignored = 0;      // spin-forbidden entries
  std::string error;
};

// Reads the stored integrals, keyed by symmetry class. The file stores
// <ij|kl> = (ik|jl).
StoredEri load_eri(const TrexioFile& file, const std::string& group, int dim,
                   const std::function<bool(int, int, int, int)>& ignore) {
  StoredEri out;
  const std::vector<SparseEri> raw = read_sparse_eri(file, group);
  out.values.reserve(raw.size());
  for (const SparseEri& e : raw) {
    const int idx[4] = {e.i, e.j, e.k, e.l};
    for (int x : idx) {
      if (x < 0 || x >= dim) {
        out.error = "stored index <" + std::to_string(e.i) + "," + std::to_string(e.j) + "|" + std::to_string(e.k) +
                    "," + std::to_string(e.l) + "> out of range";
        return out;
      }
    }
    // chemists' (ab|cd) with a = i, b = k, c = j, d = l
    if (ignore && ignore(e.i, e.k, e.j, e.l)) {
      ++out.ignored;
      continue;
    }
    const uint64_t key = eri_key(static_cast<uint64_t>(e.i), static_cast<uint64_t>(e.k), static_cast<uint64_t>(e.j),
                                 static_cast<uint64_t>(e.l));
    auto [it, inserted] = out.values.emplace(key, e.value);
    if (!inserted) out.duplicates.update(e.value, it->second, e.i, e.j);
  }
  return out;
}

// Compares a stored set of integrals against computed ones supplied by
// visit(cb), which must call cb(a, b, c, d, value) exactly once per symmetry
// class of the chemists' integral (ab|cd).
CheckResult compare_eri(const std::string& name, double t, const StoredEri& stored,
                        const std::function<void(const std::function<void(int, int, int, int, double)>&)>& visit) {
  if (!stored.error.empty()) return {name, Status::fail, stored.error};
  Deviation dev;
  std::array<int, 4> worst{};
  long compared = 0, missing = 0;
  double max_missing = 0.0;
  visit([&](int a, int b, int c, int d, double v) {
    auto it = stored.values.find(
        eri_key(static_cast<uint64_t>(a), static_cast<uint64_t>(b), static_cast<uint64_t>(c), static_cast<uint64_t>(d)));
    if (it == stored.values.end()) {
      if (std::abs(v) > t) {
        ++missing;
        max_missing = std::max(max_missing, std::abs(v));
      }
      return;
    }
    ++compared;
    dev.update(it->second, v, compared);
    if (dev.i == compared) worst = {a, b, c, d};
  });

  const bool ok = dev.max <= t && missing == 0 && stored.duplicates.max <= t;
  std::string detail = "max |file - computed| = " + sci(dev.max) + " (tol " + sci(t) + ") over " +
                       std::to_string(compared) + " unique integrals";
  if (!(dev.max <= t)) {
    // <ac|bd> in the physicists' notation of the file
    detail += "; worst at <" + std::to_string(worst[0]) + "," + std::to_string(worst[2]) + "|" +
              std::to_string(worst[1]) + "," + std::to_string(worst[3]) + ">: file " + fmt("%.10g", dev.stored) +
              ", expected " + fmt("%.10g", dev.computed);
  }
  if (missing > 0) {
    detail += "; " + std::to_string(missing) + " integrals larger than the tolerance are missing (largest " +
              sci(max_missing) + ")";
  }
  if (stored.duplicates.max > t) {
    detail += "; symmetry-equivalent stored integrals disagree by up to " + sci(stored.duplicates.max);
  }
  if (stored.ignored > 0) detail += "; " + std::to_string(stored.ignored) + " spin-forbidden entries not checked";
  return {name, ok ? Status::pass : Status::fail, detail};
}

CheckResult Runner::check_ao_eri() {
  const std::string name = "ao_2e_int_eri";
  if (!data_.has_ao_2e_int_eri) return absent(name, "ao_2e_int.eri not present");
  if (!basis_) return absent(name, basis_unavailable());

  const StoredEri stored = load_eri(file_, "ao", data_.ao_num, nullptr);
  const long n = data_.ao_num;
  return compare_eri(name, tol(name), stored, [&](const std::function<void(int, int, int, int, double)>& cb) {
    basis_->for_each_eri_block([&](int P, int Q, int R, int S, const std::vector<double>& block) {
      const auto& ap = basis_->shell_aos(P);
      const auto& aq = basis_->shell_aos(Q);
      const auto& ar = basis_->shell_aos(R);
      const auto& as = basis_->shell_aos(S);
      size_t idx = 0;
      // Within the block, keep one representative of each symmetry class.
      for (int l : as)
        for (int k : ar)
          for (int j : aq)
            for (int i : ap) {
              const double v = block[idx++];
              if (P == Q && i < j) continue;
              if (R == S && k < l) continue;
              if (P == R && Q == S && i * n + j < k * n + l) continue;
              cb(i, j, k, l, v);
            }
    });
  });
}

CheckResult Runner::check_mo_eri() {
  const std::string name = "mo_2e_int_eri";
  if (!data_.has_mo_2e_int_eri) return absent(name, "mo_2e_int.eri not present");
  if (data_.mo_coefficient.empty()) return absent(name, "mo.coefficient not present");
  if (!data_.mo_coefficient_im.empty()) return absent(name, "complex MO coefficients are not supported");
  if (!basis_) return absent(name, basis_unavailable());
  const int n = data_.ao_num, m = data_.mo_num;
  if (std::max(n, m) > opt_.max_dense_eri_dim) {
    return absent(name, "skipped since ao.num or mo.num exceeds --max-eri-dim " +
                            std::to_string(opt_.max_dense_eri_dim));
  }

  auto forbidden = [this](int a, int b, int c, int d) { return spin_mismatch(a, b) || spin_mismatch(c, d); };
  const StoredEri stored = load_eri(file_, "mo", m, forbidden);

  // (ab|cd) = sum C_a,mu C_b,nu C_c,la C_d,si (mu nu|la si)
  Matrix c(m, n);
  c.data = data_.mo_coefficient;
  std::vector<double> t1 = basis_->dense_eri(), t2;
  std::array<int, 4> dims = {n, n, n, n};
  for (int axis = 0; axis < 4; ++axis) {
    transform_axis(t1, dims, axis, c, t2);
    std::swap(t1, t2);
  }
  const std::vector<double>& mo = t1;
  return compare_eri(name, tol(name), stored, [&](const std::function<void(int, int, int, int, double)>& cb) {
    for (int a = 0; a < m; ++a)
      for (int b = 0; b <= a; ++b)
        for (int cc = 0; cc <= a; ++cc)
          for (int d = 0; d <= cc; ++d) {
            if (cc * (cc + 1) / 2 + d > a * (a + 1) / 2 + b) break;
            if (forbidden(a, b, cc, d)) continue;
            const size_t M = static_cast<size_t>(m);
            cb(a, b, cc, d, mo[static_cast<size_t>(a) + M * (b + M * (cc + M * static_cast<size_t>(d)))]);
          }
  });
}

}  // namespace

std::vector<CheckResult> run_checks(const TrexioFile& file, const Options& options) {
  Runner runner(file, options);
  return runner.run();
}

}  // namespace tv
