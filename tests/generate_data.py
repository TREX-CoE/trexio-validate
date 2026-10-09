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
import shutil
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


def write_bad_ao_counts(p, water_cart):
    """Files whose ao.num disagrees with the shells of the basis set."""
    # Cartesian AOs labelled as spherical.
    f = p("bad_ao_cartesian_flag.h5")
    write(f, water_cart)
    with trexio.File(f, "u", back_end=trexio.TREXIO_HDF5) as tf:
        ao_num = trexio.read_ao_num(tf)
        ao_shell = trexio.read_ao_shell(tf)
        norm = trexio.read_ao_normalization(tf)
        trexio.delete_ao(tf)
        trexio.write_ao_cartesian(tf, 0)
        trexio.write_ao_num(tf, ao_num)
        trexio.write_ao_shell(tf, ao_shell)
        trexio.write_ao_normalization(tf, norm)

    # One AO too many, consistently in every array that depends on ao.num.
    f = p("bad_ao_num.h5")
    write(f, water_cart)
    with trexio.File(f, "u", back_end=trexio.TREXIO_HDF5) as tf:
        ao_num = trexio.read_ao_num(tf)
        ao_shell = list(trexio.read_ao_shell(tf))
        norm = list(trexio.read_ao_normalization(tf))
        c = np.array(trexio.read_mo_coefficient(tf))
        trexio.delete_ao(tf)
        trexio.delete_mo(tf)
        trexio.write_ao_cartesian(tf, 1)
        trexio.write_ao_num(tf, ao_num + 1)
        trexio.write_ao_shell(tf, ao_shell + [ao_shell[-1]])
        trexio.write_ao_normalization(tf, norm + [1.0])
        trexio.write_mo_num(tf, c.shape[0])
        trexio.write_mo_coefficient(tf, np.ascontiguousarray(np.hstack([c, np.zeros((c.shape[0], 1))])))


def write_ao_cartesian_shell(path, flags):
    """Write ao.cartesian_shell, with a TREXIO new enough to have it.

    The trexio Python module is used if it has the field; otherwise the C
    library named by the TREXIO_LIBRARY environment variable is called through
    ctypes.
    """
    flags = np.ascontiguousarray(flags, dtype=np.int32)
    if hasattr(trexio, "write_ao_cartesian_shell"):
        with trexio.File(path, "u", back_end=trexio.TREXIO_HDF5) as tf:
            trexio.write_ao_cartesian_shell(tf, flags)
        return
    import ctypes
    lib = ctypes.CDLL(os.environ["TREXIO_LIBRARY"])
    lib.trexio_open.restype = ctypes.c_void_p
    lib.trexio_open.argtypes = [ctypes.c_char_p, ctypes.c_char, ctypes.c_int32, ctypes.POINTER(ctypes.c_int32)]
    lib.trexio_write_ao_cartesian_shell.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    lib.trexio_close.argtypes = [ctypes.c_void_p]
    rc = ctypes.c_int32()
    f = lib.trexio_open(path.encode(), b"u", 0, ctypes.byref(rc))  # 0: TREXIO_HDF5
    assert f and rc.value == 0, rc.value
    assert lib.trexio_write_ao_cartesian_shell(f, flags.ctypes.data) == 0
    assert lib.trexio_close(f) == 0


def rewrite_ao_group(tf, ao_num, ao_shell, normalization, cartesian=None):
    trexio.delete_ao(tf)
    if cartesian is not None:
        trexio.write_ao_cartesian(tf, cartesian)
    trexio.write_ao_num(tf, ao_num)
    trexio.write_ao_shell(tf, np.ascontiguousarray(ao_shell, dtype=np.int32))
    trexio.write_ao_normalization(tf, np.ascontiguousarray(normalization))


def write_mixed_shells(p):
    """Files whose shells are typed one by one in ao.cartesian_shell.

    s and p functions are the same in both conventions (S_1^{+1} = x, ...),
    only the order of the p components differs: z, x, y for spherical and
    x, y, z for Cartesian shells. Marking the s and p shells of the spherical
    water file as Cartesian, and reordering the p components in every array
    indexed by AOs, therefore gives a valid file with mixed shell types.
    """
    src = p("h2o_631gs_sph.h5")
    with trexio.File(src, "r", back_end=trexio.TREXIO_HDF5) as tf:
        ang_mom = np.array(trexio.read_basis_shell_ang_mom(tf))
        ao_num = trexio.read_ao_num(tf)
        ao_shell = np.array(trexio.read_ao_shell(tf))
        norm = np.array(trexio.read_ao_normalization(tf))
        mo = {f: getattr(trexio, "read_mo_" + f)(tf)
              for f in ["type", "num", "coefficient", "occupation", "energy", "spin", "class", "symmetry", "k_point"]
              if getattr(trexio, "has_mo_" + f)(tf)}
        ao_1e = {f: np.array(getattr(trexio, "read_ao_1e_int_" + f)(tf))
                 for f in ["overlap", "kinetic", "potential_n_e", "core_hamiltonian",
                           "dipole_x", "dipole_y", "dipole_z"]
                 if getattr(trexio, "has_ao_1e_int_" + f)(tf)}
        n = trexio.read_ao_2e_int_eri_size(tf)
        eri_idx, eri_val, _, _ = trexio.read_ao_2e_int_eri(tf, 0, n)

    # new AO i is old AO new_to_old[i]
    new_to_old = np.arange(ao_num)
    for s in np.flatnonzero(ang_mom == 1):
        z, x, y = np.flatnonzero(ao_shell == s)
        new_to_old[[z, x, y]] = [x, y, z]
    old_to_new = np.argsort(new_to_old)
    flags = (ang_mom <= 1).astype(np.int32)

    def write(name, flags, cartesian=None):
        f = p(name)
        shutil.copyfile(src, f)
        with trexio.File(f, "u", back_end=trexio.TREXIO_HDF5) as tf:
            rewrite_ao_group(tf, ao_num, ao_shell, norm[new_to_old])
            trexio.delete_mo(tf)
            for field, value in mo.items():
                if field == "coefficient":
                    value = np.ascontiguousarray(np.array(value)[:, new_to_old])
                getattr(trexio, "write_mo_" + field)(tf, value)
            trexio.delete_ao_1e_int(tf)
            for field, m in ao_1e.items():
                getattr(trexio, "write_ao_1e_int_" + field)(tf, np.ascontiguousarray(m[np.ix_(new_to_old, new_to_old)]))
            trexio.delete_ao_2e_int(tf)
            idx = old_to_new[np.asarray(eri_idx).reshape(-1, 4)].astype(np.int32)
            trexio.write_ao_2e_int_eri(tf, 0, len(eri_val), np.ascontiguousarray(idx.ravel()),
                                       np.ascontiguousarray(eri_val))
        write_ao_cartesian_shell(f, flags)
        if cartesian is not None:
            # Newer TREXIO refuses to close a file with both fields, so
            # ao.cartesian is added by a TREXIO that does not know
            # ao.cartesian_shell, as an older program would.
            assert not hasattr(trexio, "write_ao_cartesian_shell")
            with trexio.File(f, "u", back_end=trexio.TREXIO_HDF5) as tf:
                trexio.write_ao_cartesian(tf, cartesian)
        print("wrote", f)

    # Valid: s and p shells Cartesian, d shells spherical.
    write("h2o_631gs_mixed.h5", flags)
    # Invalid: the d shell marked Cartesian, which does not match ao.num.
    write("bad_ao_cartesian_shell.h5", np.ones_like(flags))
    # Invalid: ao.cartesian as well.
    write("bad_ao_both_cartesian.h5", flags, cartesian=1)

    # Invalid: neither ao.cartesian nor ao.cartesian_shell.
    f = p("bad_ao_no_cartesian.h5")
    shutil.copyfile(p("h2o_631gs_cart.h5"), f)
    with trexio.File(f, "u", back_end=trexio.TREXIO_HDF5) as tf:
        rewrite_ao_group(tf, trexio.read_ao_num(tf), trexio.read_ao_shell(tf), trexio.read_ao_normalization(tf))
    print("wrote", f)


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

    write_bad_ao_counts(p, water_cart)
    write_mixed_shells(p)

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
