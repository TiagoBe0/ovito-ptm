#!/usr/bin/env python3
"""Que observable SI distingue el ciclo 0 del ciclo 100 en una red sp3 percolante?

La componente conexa es degenerada (98.5% -> 94.6%). Se prueban candidatos que
no dependen de "estan conectados?" sino de "que tan robusta es la conexion":

  - k-core del subgrafo sp3: subgrafo maximal donde cada atomo conserva >= k
    vecinos sp3. Erosiona mucho antes que la componente conexa.
  - grado interno: cuantos de los vecinos de un atomo sp3 son tambien sp3.
  - coordinacion media <r> y conteo de Maxwell (umbral de rigidez de
    Phillips-Thorpe en <r> = 2.4).
  - tetraedricidad q de Chau-Hardwick por atomo sp3.
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


def load(cyc):
    f = parse_lammps_data(f"{D}/cycle{cyc}.data")
    L = f.box_lengths
    pos = f.positions - np.floor(f.positions / L) * L
    tree = cKDTree(pos, boxsize=L)
    pairs = tree.query_pairs(r=CUT, output_type="ndarray")
    n = len(pos)
    cn = np.bincount(pairs.ravel(), minlength=n)
    return pos, L, pairs, cn, n


def kcore_sizes(pairs, mask, n, ks=(2, 3, 4)):
    """Tamano del k-core del subgrafo inducido por `mask`."""
    sel = mask[pairs[:, 0]] & mask[pairs[:, 1]]
    e = pairs[sel]
    out = {}
    for k in ks:
        alive = mask.copy()
        ee = e
        while True:
            deg = np.bincount(ee.ravel(), minlength=n)
            drop = alive & (deg < k)
            if not drop.any():
                break
            alive &= ~drop
            ee = ee[alive[ee[:, 0]] & alive[ee[:, 1]]]
        out[k] = int(alive.sum())
    return out


def largest_cc(pairs, mask, n):
    sel = mask[pairs[:, 0]] & mask[pairs[:, 1]]
    e = pairs[sel]
    idx = np.where(mask)[0]
    remap = -np.ones(n, dtype=np.int64)
    remap[idx] = np.arange(len(idx))
    if len(e) == 0:
        return 0, len(idx)
    g = coo_matrix((np.ones(len(e)), (remap[e[:, 0]], remap[e[:, 1]])), shape=(len(idx), len(idx)))
    ncomp, lab = connected_components(g, directed=False)
    return int(np.bincount(lab).max()), ncomp


def tetrahedrality(pos, L, pairs, mask, n, sample=20000):
    """q de Chau-Hardwick promedio sobre atomos sp3 (1 = tetraedro perfecto)."""
    nbrs = [[] for _ in range(n)]
    for a, b in pairs:
        nbrs[a].append(b)
        nbrs[b].append(a)
    idx = np.where(mask)[0]
    rng = np.random.default_rng(0)
    if len(idx) > sample:
        idx = rng.choice(idx, sample, replace=False)
    qs = []
    for i in idx:
        nb = nbrs[i]
        if len(nb) != 4:
            continue
        v = pos[nb] - pos[i]
        v -= np.round(v / L) * L
        v /= np.linalg.norm(v, axis=1)[:, None]
        s = 0.0
        for j in range(3):
            for k in range(j + 1, 4):
                s += (np.dot(v[j], v[k]) + 1 / 3) ** 2
        qs.append(1 - 3 / 8 * s)
    return float(np.mean(qs)), float(np.std(qs))


print(f"{'observable':<44}{'ciclo 0':>14}{'ciclo 100':>14}{'cambio':>12}")
print("-" * 84)
res = {}
for cyc in (0, 100):
    pos, L, pairs, cn, n = load(cyc)
    sp3 = cn == 4
    lcc, ncomp = largest_cc(pairs, sp3, n)
    cores = kcore_sizes(pairs, sp3, n)
    # grado interno: vecinos sp3 de cada atomo sp3
    sel = sp3[pairs[:, 0]] & sp3[pairs[:, 1]]
    intdeg = np.bincount(pairs[sel].ravel(), minlength=n)
    q, qsd = tetrahedrality(pos, L, pairs, sp3, n)
    res[cyc] = dict(
        n_sp3=int(sp3.sum()), lcc=lcc, lcc_frac=lcc / sp3.sum(),
        core2=cores[2], core3=cores[3], core4=cores[4],
        core4_frac=cores[4] / sp3.sum(),
        intdeg=float(intdeg[sp3].mean()),
        meancoord=float(cn.mean()), q=q,
    )

rows = [
    ("atomos sp3", "n_sp3", "{:,.0f}"),
    ("componente conexa mayor", "lcc", "{:,.0f}"),
    ("  ... como fraccion de sp3", "lcc_frac", "{:.4f}"),
    ("3-core del subgrafo sp3", "core3", "{:,.0f}"),
    ("4-core del subgrafo sp3", "core4", "{:,.0f}"),
    ("  ... como fraccion de sp3", "core4_frac", "{:.4f}"),
    ("grado interno medio (vecinos sp3)", "intdeg", "{:.3f}"),
    ("coordinacion media <r> (todos)", "meancoord", "{:.4f}"),
    ("tetraedricidad media q", "q", "{:.4f}"),
]
for label, key, fmt in rows:
    a, b = res[0][key], res[100][key]
    chg = (b - a) / a * 100 if a else 0
    print(f"{label:<44}{fmt.format(a):>14}{fmt.format(b):>14}{chg:>11.1f}%")
