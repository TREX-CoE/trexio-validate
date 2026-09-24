# trexio-validate

`trexio-validate` checks the contents of [TREXIO](https://github.com/TREX-CoE/trexio)
files by recomputing them from the basis set stored in the file, using
[libcint](https://github.com/sunqm/libcint). It is meant to be run from the
test suites of programs that write TREXIO files, and is consumable from CMake.

Only Gaussian basis sets of molecular (non-periodic) systems are supported so
far.

## Checks

| Check | What is verified |
|---|---|
| `basis` | basis and AO data are consistent (index ranges, number of AOs per shell, ...) and supported |
| `nucleus_repulsion` | `nucleus.repulsion` matches the charges and coordinates |
| `electron_count` | `electron.num = up_num + dn_num`; `mo.occupation` sums to the electron count (per spin when `mo.spin` is present) |
| `mo_orthonormality` | C<sup>†</sup> S C = 1 with S computed from the basis set |
| `ao_1e_int_<op>` | stored AO integrals match the recomputed ones |
| `mo_1e_int_<op>` | stored MO integrals match C X C<sup>T</sup> |
| `ao_2e_int_eri` | every stored ERI matches, symmetry-equivalent entries agree, and no integral above the tolerance is missing |
| `mo_2e_int_eri` | the same for the MO ERIs (4-index transformation of the full AO tensor) |

`<op>` is one of `overlap`, `kinetic`, `potential_n_e`, `core_hamiltonian`,
`dipole_x`, `dipole_y`, `dipole_z`. By default, a check whose data is absent is
skipped; `--require` turns that into a failure.

The MO orthonormality check alone already tests most of the basis-set
description: exponents, contraction coefficients, all three normalization
factors, the AO ordering and the solid-harmonic convention. When it fails, the
output says whether the MOs are merely unnormalized (a normalization-factor
problem), and whether they are orthonormal with the overlap matrix stored in
the file (the basis in the file differs from the one the program used).

For open-shell files, MO pairs with different `mo.spin` are not compared, nor
are spin-forbidden MO integrals.

### Conventions

Everything is computed from the definitions of the TREXIO specification
(`trex.org`), never from the conventions of a particular program:

* the contraction coefficients are N<sub>s</sub> f<sub>ks</sub> a<sub>ks</sub>
  (`shell_factor`, `prim_factor`, `coefficient`), and AO *i* carries the extra
  factor N'<sub>i</sub> (`ao.normalization`);
* Cartesian AOs are the monomials x<sup>a</sup>y<sup>b</sup>z<sup>c</sup> in
  alphabetical order; spherical AOs are the Racah-normalized real regular solid
  harmonics S<sub>l</sub><sup>m</sup> in the order 0, +1, −1, …, +l, −l, with
  the phases of the specification's table;
* `ao.shell` gives the shell of each AO; the components of a shell follow the
  order in which its AOs appear;
* the ERIs are stored in physicists' notation, ⟨ij|kl⟩ = (ik|jl);
* the dipole operators are μ<sub>x</sub> = −x etc. about the origin.

libcint is used only for integrals over unnormalized Cartesian primitives. The
Cartesian-to-TREXIO transformation is implemented here, and it is unit-tested
against the specification's table of solid harmonics and against analytic
overlap integrals.

## Usage

```
trexio-validate [options] FILE...

  -t, --tolerance X      absolute tolerance of all comparisons (default 1e-8)
      --tol CHECK=X      tolerance for one check (repeatable)
  -c, --checks A,B,...   run only these checks
  -s, --skip A,B,...     do not run these checks
  -r, --require A,B,...  fail instead of skipping when data is missing ('all')
      --max-eri-dim N    size limit for checks needing the full ERI tensor (64)
  -q, --quiet            print only failures and the summary
  -l, --list-checks      list the available checks
```

Exit status: 0 if every check that could run passed, 1 if any failed, 2 on
usage errors, and 77 if nothing could be checked (e.g. a Slater-type basis).
The CMake helper maps 77 to a skipped test.

Example output for a file whose AO integrals are in the wrong order:

```
  PASS  mo_orthonormality            max |C^+ S C - 1| = 1.56e-14 (tol 1.00e-08); ...
  FAIL  ao_1e_int_overlap            max |file - computed| = 8.19e-01 (tol 1.00e-08); worst at [8,14]:
        found -0.3575141288, expected 0.461568113; AO 8 (shell 4, l=1, component 2, nucleus 0), ...
```

## Building

Requirements: CMake ≥ 3.21, a C++17 compiler, TREXIO (the C library), and
libcint (or its API-compatible fork qcint).

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/trexio
cmake --build build
ctest --test-dir build
cmake --install build --prefix /path/to/prefix
```

TREXIO is found through its CMake package, pkg-config, or plain
`find_library`/`find_path` (`TREXIO_INCLUDE_DIR`, `TREXIO_LIBRARY`); libcint
through `LIBCINT_INCLUDE_DIR` and `LIBCINT_LIBRARY`.

To build without installed dependencies, set `TREXIO_VALIDATE_FETCH_TREXIO=ON`
and/or `TREXIO_VALIDATE_FETCH_LIBCINT=ON`. These download TREXIO 2.6.1 and
libcint 6.1.3 and link them statically. TREXIO 2.6 needs a Fortran compiler to
configure, and HDF5 to read `.h5` files.

## Using it from another project's test suite

```cmake
find_package(TrexioValidate REQUIRED)          # installed, or:
# FetchContent_Declare(TrexioValidate
#   GIT_REPOSITORY https://github.com/susilehtola/trexio-validate.git
#   GIT_TAG master)
# FetchContent_MakeAvailable(TrexioValidate)

# A test of your project that writes out.h5
add_test(NAME scf_water COMMAND my_program water.inp)
set_tests_properties(scf_water PROPERTIES FIXTURES_SETUP water_trexio)

trexio_validate_add_test(validate_water
  FILE "${CMAKE_CURRENT_BINARY_DIR}/out.h5"
  FIXTURES_REQUIRED water_trexio
  REQUIRE mo_orthonormality ao_1e_int_overlap
  TOLERANCE 1e-9)
```

`trexio_validate_add_test` also accepts `CHECKS`, `SKIP`, `DEPENDS`,
`LABELS`, `WORKING_DIRECTORY` and `EXTRA_ARGS`; see
[cmake/TrexioValidate.cmake](cmake/TrexioValidate.cmake). The executable is
available as the target `TrexioValidate::trexio-validate` for custom use.
[examples/consumer](examples/consumer) is a complete example of both ways of
consuming the project.

When included as a subproject, the project neither builds its own tests nor
installs anything, unless `TREXIO_VALIDATE_BUILD_TESTS` /
`TREXIO_VALIDATE_INSTALL` are set.

## Test data

`tests/data` holds files written by the TREXIO interface of
[pyscf-forge](https://github.com/pyscf/pyscf-forge) (`pyscf/tools/trexio.py`),
an independent producer, together with deliberately broken copies. They are
regenerated with `tests/generate_data.py` (needs pyscf and the TREXIO Python
module built with HDF5).

pyscf-forge's writer stores `mo.coefficient` in TREXIO's AO order, but
`ao_1e_int` and `ao_2e_int` in PySCF's own AO order
([pyscf-forge#214](https://github.com/pyscf/pyscf-forge/issues/214)). The
generator therefore reorders the AO integrals of the valid files, and keeps the
unmodified output as `bad_pyscf_forge_ao_integrals.h5`, which must fail.

## Not yet supported

* effective core potentials (`ao_1e_int.ecp`, and `core_hamiltonian` when an
  ECP is present, are skipped);
* Cholesky-decomposed and long-range ERIs;
* periodic systems, complex basis functions or MO integrals, `r_power ≠ 0`,
  Slater-type and numerical orbitals;
* consistency of determinants, CSFs, RDMs and other wave-function data.

## License

BSD 3-Clause, like TREXIO; see [LICENSE](LICENSE).
