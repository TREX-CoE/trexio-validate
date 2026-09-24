#!/usr/bin/env python3
"""Generate the TREXIO test files of trexio-validate.

The files are written by the TREXIO interface of pyscf-forge
(pyscf/tools/trexio.py), i.e. by a producer that is independent of
trexio-validate. Corrupted copies are made for the negative tests.

Requirements: pyscf, trexio (Python), and pyscf-forge's trexio.py, e.g.

    PYSCF_FORGE_TREXIO=/path/to/pyscf-forge/pyscf/tools/trexio.py \
        python3 tests/generate_data.py tests/data

The generated files are committed to the repository, so this script is only
needed to regenerate them.
"""

import importlib.util
import os
import sys

import numpy as np
import trexio
from pyscf import gto, scf


def load_pyscf_trexio():
    path = os.environ.get("PYSCF_FORGE_TREXIO")
    if path:
        spec = importlib.util.spec_from_file_location("pyscf_forge_trexio", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    from pyscf.tools import trexio as module

    return module


ptx = load_pyscf_trexio()

WATER = "O 0 0 0.1173; H 0 0.7572 -0.4692; H 0 -0.7572 -0.4692"


def run_scf(atom, basis, cart=False, spin=0, charge=0):
    mol = gto.M(atom=atom, basis=basis, cart=cart, spin=spin, charge=charge, verbose=0)
    mf = (scf.RHF(mol) if spin == 0 else scf.UHF(mol)).run(conv_tol=1e-12)
    assert mf.converged
    return mf


def write(path, mf, **kwargs):
    if os.path.exists(path):
        os.remove(path)
    ptx.to_trexio(mf, path, **kwargs)
    print("wrote", path)


def fix_ao_integrals(path, mf):
    """Put the AO integrals written by pyscf-forge into TREXIO's AO order.

    pyscf-forge writes mo.coefficient in TREXIO's AO order but the ao_1e_int
    and ao_2e_int data in PySCF's (p: x,y,z instead of z,x,y; d: -2..+2
    instead of 0,+1,-1,+2,-2). Also adds the dipole integrals, which
    pyscf-forge does not write.
    """
    mol = mf.mol
    idx = ptx._order_ao_index(mol)  # TREXIO AO t is PySCF AO idx[t]
    inv = np.argsort(idx)
    ops = ["overlap", "kinetic", "potential_n_e", "core_hamiltonian"]
    with trexio.File(path, "u", back_end=trexio.TREXIO_HDF5) as tf:
        mats = {op: getattr(trexio, "read_ao_1e_int_" + op)(tf) for op in ops}
        # TREXIO dipole operator: mu = -r
        for comp, op in enumerate(["dipole_x", "dipole_y", "dipole_z"]):
            mats[op] = -mol.intor("int1e_r")[comp]
        c = np.array(trexio.read_mo_coefficient(tf))  # [mo][ao], TREXIO order
        has_eri = trexio.has_ao_2e_int_eri(tf)
        if has_eri:
            n = trexio.read_ao_2e_int_eri_size(tf)
            eri_idx, eri_val, _, _ = trexio.read_ao_2e_int_eri(tf, 0, n)

        trexio.delete_ao_1e_int(tf)
        for op, m in mats.items():
            m = np.ascontiguousarray(np.asarray(m)[np.ix_(idx, idx)])
            getattr(trexio, "write_ao_1e_int_" + op)(tf, m)
            if op.startswith("dipole"):
                getattr(trexio, "write_mo_1e_int_" + op)(tf, np.ascontiguousarray(c @ m @ c.T))
        if has_eri:
            trexio.delete_ao_2e_int(tf)
            new_idx = inv[np.asarray(eri_idx).reshape(-1, 4)].astype(np.int32)
            trexio.write_ao_2e_int_eri(tf, 0, len(eri_val), np.ascontiguousarray(new_idx.ravel()),
                                       np.ascontiguousarray(eri_val))


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    p = lambda name: os.path.join(outdir, name)

    # --- valid files -----------------------------------------------------
    # Spherical basis with s, p, d; all integrals the writer supports.
    water = run_scf(WATER, "6-31g*")
    write(p("h2o_631gs_sph.h5"), water, write_ao_eri=True, write_mo_eri=True, eri_sym="s4")
    fix_ao_integrals(p("h2o_631gs_sph.h5"), water)

    # Cartesian basis with s, p, d.
    water_cart = run_scf(WATER, "6-31g*", cart=True)
    write(p("h2o_631gs_cart.h5"), water_cart, write_ao_eri=True, eri_sym="s8")
    fix_ao_integrals(p("h2o_631gs_cart.h5"), water_cart)

    # Spherical and Cartesian f and g functions.
    n2 = run_scf("N 0 0 0; N 0 0 2.07", "cc-pvqz")
    write(p("n2_ccpvqz_sph.h5"), n2)
    n2_cart = run_scf("N 0 0 0; N 0 0 2.07", "cc-pvtz", cart=True)
    write(p("n2_ccpvtz_cart.h5"), n2_cart)

    # Open-shell UHF with MO integrals over both spins.
    oh = run_scf("O 0 0 0; H 0 0 1.83", "6-31g", spin=1)
    write(p("oh_631g_uhf.h5"), oh, write_ao_eri=True, write_mo_eri=True, eri_sym="s4")
    fix_ao_integrals(p("oh_631g_uhf.h5"), oh)

    # --- invalid files ---------------------------------------------------
    # pyscf-forge's own output, with the AO integrals in PySCF's AO order.
    write(p("bad_pyscf_forge_ao_integrals.h5"), water, write_ao_eri=True, eri_sym="s8")

    # MO coefficients in PySCF's AO order instead of TREXIO's.
    f = p("bad_ao_order.h5")
    write(f, water)
    with trexio.File(f, "u", back_end=trexio.TREXIO_HDF5) as tf:
        good = np.array(trexio.read_mo_coefficient(tf))
        c = np.ascontiguousarray(water.mo_coeff.T)
        assert not np.allclose(c, good)
        trexio.delete_mo(tf)
        trexio.write_mo_num(tf, c.shape[0])
        trexio.write_mo_coefficient(tf, c)

    # Normalization of the d functions lost.
    f = p("bad_ao_normalization.h5")
    write(f, water)
    with trexio.File(f, "u", back_end=trexio.TREXIO_HDF5) as tf:
        norm = np.array(trexio.read_ao_normalization(tf))
        ao_shell = np.array(trexio.read_ao_shell(tf))
        l = np.array(trexio.read_basis_shell_ang_mom(tf))[ao_shell]
        norm[l == 2] = 1.0
        trexio.delete_ao(tf)
        trexio.write_ao_cartesian(tf, 0)
        trexio.write_ao_num(tf, len(norm))
        trexio.write_ao_shell(tf, ao_shell)
        trexio.write_ao_normalization(tf, norm)

    # Nuclear coordinates in angstrom.
    f = p("bad_nucleus_repulsion.h5")
    write(f, water)
    with trexio.File(f, "u", back_end=trexio.TREXIO_HDF5) as tf:
        mol = water.mol
        trexio.delete_nucleus(tf)
        trexio.write_nucleus_num(tf, mol.natm)
        trexio.write_nucleus_charge(tf, mol.atom_charges().astype(float))
        trexio.write_nucleus_coord(tf, mol.atom_coords(unit="angstrom"))
        trexio.write_nucleus_repulsion(tf, mol.energy_nuc())

    # AO ERIs with one integral changed, and with integrals left out.
    for name in ["bad_ao_eri_value.h5", "bad_ao_eri_missing.h5"]:
        f = p(name)
        write(f, water, write_ao_eri=True, eri_sym="s8")
        fix_ao_integrals(f, water)
        with trexio.File(f, "u", back_end=trexio.TREXIO_HDF5) as tf:
            n = trexio.read_ao_2e_int_eri_size(tf)
            idx, val, _, _ = trexio.read_ao_2e_int_eri(tf, 0, n)
            idx = np.asarray(idx).ravel()
            val = np.array(val)
            if name == "bad_ao_eri_value.h5":
                k = int(np.argmax(np.abs(val)))
                val[k] *= 1.001
            else:
                idx, val = idx[: 4 * (n // 2)], val[: n // 2]
            trexio.delete_ao_2e_int(tf)
            trexio.write_ao_2e_int_eri(tf, 0, len(val), np.ascontiguousarray(idx), np.ascontiguousarray(val))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "data"))
