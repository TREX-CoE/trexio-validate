// Stand-in for an application that writes TREXIO data with the in-memory back
// end: copies a TREXIO file into a TREXIO_MEMORY file, optionally corrupting
// it. Used by the C++ and Python tests of the library interface.

#include "memory_helper.h"

#include <cstring>
#include <string>

#include "trexio_data.hpp"

#ifdef TREXIO_MEMORY

namespace {

void write(trexio_exit_code rc, const char* what) { tv::check_trexio(rc, std::string("writing ") + what); }

#define TV_W(field, ...) write(trexio_write_##field(out, __VA_ARGS__), #field)

void copy(const tv::TrexioFile& in, trexio_t* out, int corruption) {
  tv::TrexioData d = tv::read_trexio_data(in);

  if (corruption == TV_MEMORY_BAD_NUCLEUS_REPULSION && d.nucleus_repulsion) *d.nucleus_repulsion += 1.0;
  if (corruption == TV_MEMORY_BAD_MO_COEFFICIENT && !d.mo_coefficient.empty()) d.mo_coefficient[0] *= 1.01;

  TV_W(nucleus_num, d.nucleus_num);
  TV_W(nucleus_charge, d.nucleus_charge.data());
  TV_W(nucleus_coord, d.nucleus_coord.data());
  if (d.nucleus_repulsion) TV_W(nucleus_repulsion, *d.nucleus_repulsion);
  if (d.electron_num) TV_W(electron_num, *d.electron_num);
  if (d.electron_up_num) TV_W(electron_up_num, *d.electron_up_num);
  if (d.electron_dn_num) TV_W(electron_dn_num, *d.electron_dn_num);

  TV_W(basis_type, d.basis_type.c_str(), static_cast<int32_t>(d.basis_type.size() + 1));
  TV_W(basis_shell_num, d.basis_shell_num);
  TV_W(basis_prim_num, d.basis_prim_num);
  TV_W(basis_nucleus_index, d.basis_nucleus_index.data());
  TV_W(basis_shell_ang_mom, d.basis_shell_ang_mom.data());
  TV_W(basis_shell_factor, d.basis_shell_factor.data());
  TV_W(basis_r_power, d.basis_r_power.data());
  TV_W(basis_shell_index, d.basis_shell_index.data());
  TV_W(basis_exponent, d.basis_exponent.data());
  TV_W(basis_coefficient, d.basis_coefficient.data());
  TV_W(basis_prim_factor, d.basis_prim_factor.data());

  TV_W(ao_cartesian, *d.ao_cartesian);
  TV_W(ao_num, d.ao_num);
  if (!d.ao_shell.empty()) TV_W(ao_shell, d.ao_shell.data());
  TV_W(ao_normalization, d.ao_normalization.data());

  if (d.mo_num > 0) {
    TV_W(mo_num, d.mo_num);
    if (!d.mo_coefficient.empty()) TV_W(mo_coefficient, d.mo_coefficient.data());
    if (!d.mo_occupation.empty()) TV_W(mo_occupation, d.mo_occupation.data());
    if (!d.mo_spin.empty()) TV_W(mo_spin, d.mo_spin.data());
  }

  // One-electron integrals.
#define TV_W1(group, op)                                              \
  do {                                                                \
    auto it = d.group.find(#op);                                      \
    if (it != d.group.end()) TV_W(group##_##op, it->second.data());  \
  } while (0)
  TV_W1(ao_1e_int, overlap);
  TV_W1(ao_1e_int, kinetic);
  TV_W1(ao_1e_int, potential_n_e);
  TV_W1(ao_1e_int, core_hamiltonian);
  TV_W1(ao_1e_int, dipole_x);
  TV_W1(ao_1e_int, dipole_y);
  TV_W1(ao_1e_int, dipole_z);
  TV_W1(mo_1e_int, overlap);
  TV_W1(mo_1e_int, kinetic);
  TV_W1(mo_1e_int, potential_n_e);
  TV_W1(mo_1e_int, core_hamiltonian);
  TV_W1(mo_1e_int, dipole_x);
  TV_W1(mo_1e_int, dipole_y);
  TV_W1(mo_1e_int, dipole_z);
#undef TV_W1

  // Two-electron integrals.
  for (const char* group : {"ao", "mo"}) {
    const bool present = std::strcmp(group, "ao") == 0 ? d.has_ao_2e_int_eri : d.has_mo_2e_int_eri;
    if (!present) continue;
    const std::vector<tv::SparseEri> eri = tv::read_sparse_eri(in, group);
    std::vector<int32_t> idx;
    std::vector<double> val;
    for (const auto& e : eri) {
      idx.insert(idx.end(), {e.i, e.j, e.k, e.l});
      val.push_back(e.value);
    }
    const int64_t n = static_cast<int64_t>(val.size());
    if (std::strcmp(group, "ao") == 0) {
      TV_W(ao_2e_int_eri, 0, n, idx.data(), val.data());
    } else {
      TV_W(mo_2e_int_eri, 0, n, idx.data(), val.data());
    }
  }
}

}  // namespace

extern "C" {

int tv_memory_available(void) { return 1; }

trexio_t* tv_memory_copy(const char* path, int corruption) {
  trexio_t* out = nullptr;
  try {
    tv::TrexioFile in(path);
    trexio_exit_code rc = TREXIO_SUCCESS;
    out = trexio_open("memory-copy", 'w', TREXIO_MEMORY, &rc);
    if (out == nullptr) return nullptr;
    copy(in, out, corruption);
    return out;
  } catch (...) {
    if (out != nullptr) trexio_close(out);
    return nullptr;
  }
}

void tv_memory_close(trexio_t* file) {
  if (file != nullptr) trexio_close(file);
}

int32_t tv_memory_nucleus_num(trexio_t* file) {
  int32_t n = -1;
  if (trexio_read_nucleus_num(file, &n) != TREXIO_SUCCESS) return -1;
  return n;
}

}  // extern "C"

#else  // no in-memory back end

extern "C" {
int tv_memory_available(void) { return 0; }
trexio_t* tv_memory_copy(const char*, int) { return nullptr; }
void tv_memory_close(trexio_t*) {}
int32_t tv_memory_nucleus_num(trexio_t*) { return -1; }
}

#endif
