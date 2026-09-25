// Reading the parts of a TREXIO file that trexio-validate knows how to check.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// TREXIO releases before 2.3 lack the extern "C" guards in trexio.h.
extern "C" {
#include <trexio.h>
}

namespace tv {

class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Throws tv::Error unless rc is TREXIO_SUCCESS.
void check_trexio(trexio_exit_code rc, const std::string& what);

// Handle of an open TREXIO file: either opened (and closed) here, or borrowed
// from the caller, e.g. a file of the in-memory back end.
class TrexioFile {
 public:
  explicit TrexioFile(const std::string& path);
  TrexioFile(trexio_t* borrowed, const std::string& label);
  ~TrexioFile();
  TrexioFile(const TrexioFile&) = delete;
  TrexioFile& operator=(const TrexioFile&) = delete;

  trexio_t* get() const { return file_; }
  const std::string& path() const { return path_; }

 private:
  trexio_t* file_ = nullptr;
  std::string path_;
  bool owned_ = true;
};

// One stored two-electron integral <ij|kl> (physicists' notation, as in the
// TREXIO sparse storage).
struct SparseEri {
  int32_t i, j, k, l;
  double value;
};

// Names of the one-electron operators stored in the ao_1e_int and mo_1e_int
// groups that can be recomputed.
extern const std::vector<std::string> one_electron_operators;

// The data read from a TREXIO file. Missing optional data is represented by
// empty vectors / optionals. Array layouts are those of the TREXIO C API
// (row-major, i.e. the last index runs fastest).
struct TrexioData {
  std::vector<std::string> warnings;

  // pbc
  bool periodic = false;

  // nucleus
  int32_t nucleus_num = 0;
  std::vector<double> nucleus_charge;  // [nucleus_num]
  std::vector<double> nucleus_coord;   // [nucleus_num][3]
  std::optional<double> nucleus_repulsion;

  // electron
  std::optional<int32_t> electron_num, electron_up_num, electron_dn_num;

  // basis
  std::string basis_type;
  int32_t basis_shell_num = 0;
  int32_t basis_prim_num = 0;
  std::vector<int32_t> basis_nucleus_index;  // [shell_num]
  std::vector<int32_t> basis_shell_ang_mom;  // [shell_num]
  std::vector<double> basis_shell_factor;    // [shell_num]
  std::vector<int32_t> basis_r_power;        // [shell_num]
  std::vector<int32_t> basis_shell_index;    // [prim_num]
  std::vector<double> basis_exponent;        // [prim_num]
  std::vector<double> basis_coefficient;     // [prim_num]
  std::vector<double> basis_prim_factor;     // [prim_num]
  bool basis_has_complex = false;            // exponent_im / coefficient_im
  bool basis_has_oscillation = false;

  // ecp
  bool has_ecp = false;

  // ao
  std::optional<int32_t> ao_cartesian;
  int32_t ao_num = 0;
  std::vector<int32_t> ao_shell;         // [ao_num]
  std::vector<double> ao_normalization;  // [ao_num]

  // mo
  int32_t mo_num = 0;
  std::vector<double> mo_coefficient;     // [mo_num][ao_num]
  std::vector<double> mo_coefficient_im;  // [mo_num][ao_num]
  std::vector<double> mo_occupation;      // [mo_num]
  std::vector<int32_t> mo_spin;           // [mo_num]
  std::vector<int32_t> mo_k_point;        // [mo_num]

  // one-electron integrals, keyed by operator name; [ao_num][ao_num] and
  // [mo_num][mo_num]
  std::map<std::string, std::vector<double>> ao_1e_int;
  std::map<std::string, std::vector<double>> mo_1e_int;

  // presence of sparse two-electron integrals (read on demand)
  bool has_ao_2e_int_eri = false;
  bool has_mo_2e_int_eri = false;
};

TrexioData read_trexio_data(const TrexioFile& file);

// Checks whose data the TREXIO library in use cannot read, with the reason.
const std::map<std::string, std::string>& unavailable_checks();

// Fields the TREXIO library in use cannot read, so that their absence from a
// file cannot be relied upon (e.g. mo.spin of an open-shell file).
std::vector<std::string> unreadable_fields();

// Reads all stored ERIs of the "ao" or "mo" group.
std::vector<SparseEri> read_sparse_eri(const TrexioFile& file, const std::string& group);

}  // namespace tv
