#!/usr/bin/env python3
"""Si en vez de clusterizar TODO atomo sp3 se clusterizan solo los que estan
bien embebidos en la red sp3 (>= t vecinos sp3), la componente conexa deja de
ser degenerada?
"""
import sys
sys.path.insert(0, "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/src")

import numpy as np
from scipy.spatial import cKDTree
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
from ac_analyzer.core.reader import parse_lammps_data

CUT = 2.064
D = "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/samples_for_analysis/camorfo_sp3_60_mCRA"


def analyse(cyc):
    f = parse_lammps_data(f"{D}/cycle{cyc}.data")
    L = f.box_lengths
    pos = f.positions - np.floor(f.positions / L) * L
    pairs = cKDTree(pos, boxsize=L).query_pairs(r=CUT, output_type="ndarray")
    n = len(pos)
    cn = np.bincount(pairs.ravel(), minlength=n)
    sp3 = cn == 4
    sel = sp3[pairs[:, 0]] & sp3[pairs[:, 1]]
    e_sp3 = pairs[sel]
    intdeg = np.bincount(e_sp3.ravel(), minlength=n)

    out = {"n_sp3": int(sp3.sum())}
    # distribucion del grado interno
    for d in range(5):
        out[f"intdeg{d}"] = int(((intdeg == d) & sp3).sum())
    # 2-core (poda de extremos colgantes)
    alive, ee = sp3.copy(), e_sp3
    while True:
        deg = np.bincount(ee.ravel(), minlength=n)
        drop = alive & (deg < 2)
        if not drop.any():
            break
        alive &= ~drop
        ee = ee[alive[ee[:, 0]] & alive[ee[:, 1]]]
    out["core2"] = int(alive.sum())

    # componente conexa sobre el nucleo: sp3 con >= t vecinos sp3
    for t in (1, 2, 3, 4):
        mask = sp3 & (intdeg >= t)
        m = mask[pairs[:, 0]] & mask[pairs[:, 1]]
        e = pairs[m]
        idx = np.where(mask)[0]
        if len(idx) == 0 or len(e) == 0:
            out[f"t{t}_n"], out[f"t{t}_lcc"], out[f"t{t}_ncl"] = len(idx), 0, len(idx)
            continue
        remap = -np.ones(n, dtype=np.int64)
        remap[idx] = np.arange(len(idx))
        g = coo_matrix((np.ones(len(e)), (remap[e[:, 0]], remap[e[:, 1]])), shape=(len(idx), len(idx)))
        ncomp, lab = connected_components(g, directed=False)
        sizes = np.bincount(lab)
        out[f"t{t}_n"] = len(idx)
        out[f"t{t}_lcc"] = int(sizes.max())
        out[f"t{t}_ncl"] = int(ncomp)
    return out


a, b = analyse(0), analyse(100)

print("DISTRIBUCION DEL GRADO INTERNO (vecinos sp3 de cada atomo sp3)")
print(f"{'vecinos sp3':<16}{'ciclo 0':>14}{'ciclo 100':>14}{'cambio':>12}")
for d in range(5):
    x, y = a[f"intdeg{d}"], b[f"intdeg{d}"]
    print(f"{d:<16}{x:>14,}{y:>14,}{(y-x)/x*100 if x else 0:>11.1f}%")

print(f"\n2-core del subgrafo sp3: {a['core2']:,} -> {b['core2']:,} "
      f"({(b['core2']-a['core2'])/a['core2']*100:.1f}%)")

print("\nCLUSTERIZAR SOLO EL NUCLEO: sp3 con >= t vecinos sp3")
print(f"{'t':<4}{'atomos c0':>12}{'atomos c100':>13}{'lcc/n c0':>11}{'lcc/n c100':>12}"
      f"{'cambio lcc':>13}{'n clusters c0':>15}{'c100':>9}")
for t in (1, 2, 3, 4):
    n0, n1 = a[f"t{t}_n"], b[f"t{t}_n"]
    l0, l1 = a[f"t{t}_lcc"], b[f"t{t}_lcc"]
    f0 = l0 / n0 if n0 else 0
    f1 = l1 / n1 if n1 else 0
    chg = (l1 - l0) / l0 * 100 if l0 else 0
    print(f"{t:<4}{n0:>12,}{n1:>13,}{f0:>11.4f}{f1:>12.4f}{chg:>12.1f}%"
          f"{a[f't{t}_ncl']:>15,}{b[f't{t}_ncl']:>9,}")
