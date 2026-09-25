#include "trexio_data.hpp"

#include <algorithm>

namespace tv {

const std::vector<std::string> one_electron_operators = {
    "overlap",          "kinetic",  "potential_n_e", "core_hamiltonian",
    "dipole_x",         "dipole_y", "dipole_z",
};

void check_trexio(trexio_exit_code rc, const std::string& what) {
  if (rc != TREXIO_SUCCESS) {
    throw Error("TREXIO error while reading " + what + ": " + trexio_string_of_error(rc));
  }
}

TrexioFile::TrexioFile(const std::string& path) : path_(path) {
  trexio_exit_code rc = TREXIO_SUCCESS;
  file_ = trexio_open(path.c_str(), 'r', TREXIO_AUTO, &rc);
  if (file_ == nullptr || rc != TREXIO_SUCCESS) {
    throw Error("cannot open TREXIO file '" + path + "': " + trexio_string_of_error(rc));
  }
}

TrexioFile::TrexioFile(trexio_t* borrowed, const std::string& label)
    : file_(borrowed), path_(label), owned_(false) {
  if (file_ == nullptr) throw Error("null TREXIO file handle");
}

TrexioFile::~TrexioFile() {
  if (owned_ && file_ != nullptr) trexio_close(file_);
}

namespace {

bool has(trexio_exit_code rc, const char* what) {
  if (rc == TREXIO_SUCCESS) return true;
  if (rc == TREXIO_HAS_NOT) return false;
  check_trexio(rc, what);
  return false;
}

}  // namespace

// The generated TREXIO API has one function per field, hence the macros.
#define TV_HAS(field) has(trexio_has_##field(f), #field)

#define TV_READ_SCALAR(field, dest)                            \
  do {                                                         \
    check_trexio(trexio_read_##field(f, &(dest)), #field);     \
  } while (0)

#define TV_READ_OPT_SCALAR(field, dest, type)                  \
  do {                                                         \
    if (TV_HAS(field)) {                                       \
      type tmp_{};                                             \
      check_trexio(trexio_read_##field(f, &tmp_), #field);     \
      (dest) = tmp_;                                           \
    }                                                          \
  } while (0)

#define TV_READ_ARRAY(field, dest, size)                       \
  do {                                                         \
    (dest).resize(static_cast<size_t>(size));                  \
    check_trexio(trexio_read_##field(f, (dest).data()), #field); \
  } while (0)

#define TV_READ_OPT_ARRAY(field, dest, size)                   \
  do {                                                         \
    if (TV_HAS(field)) TV_READ_ARRAY(field, dest, size);       \
  } while (0)

#define TV_READ_1E(group, op, dest_map, n)                               \
  do {                                                                   \
    if (TV_HAS(group##_##op)) {                                          \
      auto& m_ = (dest_map)[#op];                                        \
      m_.resize(static_cast<size_t>(n) * static_cast<size_t>(n));        \
      check_trexio(trexio_read_##group##_##op(f, m_.data()), #group "_" #op); \
    }                                                                    \
  } while (0)

TrexioData read_trexio_data(const TrexioFile& file) {
  trexio_t* f = file.get();
  TrexioData d;

  if (TV_HAS(pbc_periodic)) {
    int32_t periodic = 0;
    TV_READ_SCALAR(pbc_periodic, periodic);
    d.periodic = periodic != 0;
  }

  // nucleus
  TV_READ_SCALAR(nucleus_num, d.nucleus_num);
  TV_READ_ARRAY(nucleus_charge, d.nucleus_charge, d.nucleus_num);
  TV_READ_ARRAY(nucleus_coord, d.nucleus_coord, 3 * d.nucleus_num);
  TV_READ_OPT_SCALAR(nucleus_repulsion, d.nucleus_repulsion, double);

  // electron
  TV_READ_OPT_SCALAR(electron_num, d.electron_num, int32_t);
  TV_READ_OPT_SCALAR(electron_up_num, d.electron_up_num, int32_t);
  TV_READ_OPT_SCALAR(electron_dn_num, d.electron_dn_num, int32_t);

  // basis
  {
    char buf[256] = {0};
    check_trexio(trexio_read_basis_type(f, buf, sizeof(buf) - 1), "basis_type");
    d.basis_type = buf;
    // Strip trailing blanks (Fortran-written strings may be padded).
    while (!d.basis_type.empty() && d.basis_type.back() == ' ') d.basis_type.pop_back();
  }
  TV_READ_SCALAR(basis_shell_num, d.basis_shell_num);
  TV_READ_SCALAR(basis_prim_num, d.basis_prim_num);
  const int32_t nsh = d.basis_shell_num;
  const int32_t npr = d.basis_prim_num;
  TV_READ_ARRAY(basis_nucleus_index, d.basis_nucleus_index, nsh);
  TV_READ_ARRAY(basis_shell_ang_mom, d.basis_shell_ang_mom, nsh);
  TV_READ_ARRAY(basis_shell_index, d.basis_shell_index, npr);
  TV_READ_ARRAY(basis_exponent, d.basis_exponent, npr);
  TV_READ_ARRAY(basis_coefficient, d.basis_coefficient, npr);
  if (TV_HAS(basis_shell_factor)) {
    TV_READ_ARRAY(basis_shell_factor, d.basis_shell_factor, nsh);
  } else {
    d.basis_shell_factor.assign(static_cast<size_t>(nsh), 1.0);
    d.warnings.push_back("basis_shell_factor is missing; assuming 1");
  }
  if (TV_HAS(basis_prim_factor)) {
    TV_READ_ARRAY(basis_prim_factor, d.basis_prim_factor, npr);
  } else {
    d.basis_prim_factor.assign(static_cast<size_t>(npr), 1.0);
    d.warnings.push_back("basis_prim_factor is missing; assuming 1");
  }
  if (TV_HAS(basis_r_power)) {
    TV_READ_ARRAY(basis_r_power, d.basis_r_power, nsh);
  } else {
    d.basis_r_power.assign(static_cast<size_t>(nsh), 0);
  }
  d.basis_has_complex = TV_HAS(basis_exponent_im) || TV_HAS(basis_coefficient_im);
  d.basis_has_oscillation = TV_HAS(basis_oscillation_arg);

  d.has_ecp = TV_HAS(ecp);

  // ao
  TV_READ_OPT_SCALAR(ao_cartesian, d.ao_cartesian, int32_t);
  TV_READ_SCALAR(ao_num, d.ao_num);
  TV_READ_OPT_ARRAY(ao_shell, d.ao_shell, d.ao_num);
  if (TV_HAS(ao_normalization)) {
    TV_READ_ARRAY(ao_normalization, d.ao_normalization, d.ao_num);
  } else {
    d.ao_normalization.assign(static_cast<size_t>(d.ao_num), 1.0);
    d.warnings.push_back("ao_normalization is missing; assuming 1");
  }

  // mo
  if (TV_HAS(mo_num)) {
    TV_READ_SCALAR(mo_num, d.mo_num);
    const int64_t nc = static_cast<int64_t>(d.mo_num) * d.ao_num;
    TV_READ_OPT_ARRAY(mo_coefficient, d.mo_coefficient, nc);
    TV_READ_OPT_ARRAY(mo_coefficient_im, d.mo_coefficient_im, nc);
    TV_READ_OPT_ARRAY(mo_occupation, d.mo_occupation, d.mo_num);
    TV_READ_OPT_ARRAY(mo_spin, d.mo_spin, d.mo_num);
    TV_READ_OPT_ARRAY(mo_k_point, d.mo_k_point, d.mo_num);
  }

  // one-electron integrals
  TV_READ_1E(ao_1e_int, overlap, d.ao_1e_int, d.ao_num);
  TV_READ_1E(ao_1e_int, kinetic, d.ao_1e_int, d.ao_num);
  TV_READ_1E(ao_1e_int, potential_n_e, d.ao_1e_int, d.ao_num);
  TV_READ_1E(ao_1e_int, core_hamiltonian, d.ao_1e_int, d.ao_num);
  TV_READ_1E(ao_1e_int, dipole_x, d.ao_1e_int, d.ao_num);
  TV_READ_1E(ao_1e_int, dipole_y, d.ao_1e_int, d.ao_num);
  TV_READ_1E(ao_1e_int, dipole_z, d.ao_1e_int, d.ao_num);
  if (d.mo_num > 0) {
    TV_READ_1E(mo_1e_int, overlap, d.mo_1e_int, d.mo_num);
    TV_READ_1E(mo_1e_int, kinetic, d.mo_1e_int, d.mo_num);
    TV_READ_1E(mo_1e_int, potential_n_e, d.mo_1e_int, d.mo_num);
    TV_READ_1E(mo_1e_int, core_hamiltonian, d.mo_1e_int, d.mo_num);
    TV_READ_1E(mo_1e_int, dipole_x, d.mo_1e_int, d.mo_num);
    TV_READ_1E(mo_1e_int, dipole_y, d.mo_1e_int, d.mo_num);
    TV_READ_1E(mo_1e_int, dipole_z, d.mo_1e_int, d.mo_num);
  }

  d.has_ao_2e_int_eri = TV_HAS(ao_2e_int_eri);
  d.has_mo_2e_int_eri = TV_HAS(mo_2e_int_eri);
  return d;
}

namespace {

template <class SizeFn, class ReadFn>
std::vector<SparseEri> read_sparse(SizeFn size_fn, ReadFn read_fn, const std::string& what) {
  int64_t size = 0;
  check_trexio(size_fn(&size), what + " size");
  std::vector<SparseEri> out;
  out.reserve(static_cast<size_t>(size));

  const int64_t chunk = 1 << 20;
  std::vector<int32_t> idx(static_cast<size_t>(4 * chunk));
  std::vector<double> val(static_cast<size_t>(chunk));
  int64_t offset = 0;
  while (offset < size) {
    int64_t count = std::min(chunk, size - offset);
    trexio_exit_code rc = read_fn(offset, &count, idx.data(), val.data());
    if (rc != TREXIO_SUCCESS && rc != TREXIO_END) check_trexio(rc, what);
    for (int64_t n = 0; n < count; ++n) {
      out.push_back({idx[4 * n], idx[4 * n + 1], idx[4 * n + 2], idx[4 * n + 3], val[n]});
    }
    offset += count;
    if (rc == TREXIO_END || count == 0) break;
  }
  return out;
}

}  // namespace

std::vector<SparseEri> read_sparse_eri(const TrexioFile& file, const std::string& group) {
  trexio_t* f = file.get();
  if (group == "ao") {
    return read_sparse([f](int64_t* s) { return trexio_read_ao_2e_int_eri_size(f, s); },
                       [f](int64_t o, int64_t* n, int32_t* i, double* v) {
                         return trexio_read_ao_2e_int_eri(f, o, n, i, v);
                       },
                       "ao_2e_int_eri");
  }
  if (group == "mo") {
    return read_sparse([f](int64_t* s) { return trexio_read_mo_2e_int_eri_size(f, s); },
                       [f](int64_t o, int64_t* n, int32_t* i, double* v) {
                         return trexio_read_mo_2e_int_eri(f, o, n, i, v);
                       },
                       "mo_2e_int_eri");
  }
  throw Error("unknown ERI group " + group);
}

}  // namespace tv
