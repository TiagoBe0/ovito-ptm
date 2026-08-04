#!/usr/bin/env python3
"""Candidatas adicionales, medidas sobre el nucleo sp3 (>=3 vecinos sp3):

  - distribucion de tamanos de cluster: cerca de p_c debe seguir n(s) ~ s^-tau
    con tau ~ 2.19 en percolacion 3D. Si el exponente ajustado se acerca a ese
    valor en la banda de transicion, es evidencia cuantitativa de criticidad.
  - radio de giro del cluster mayor.
  - correlacion espacial del dano: los atomos que dejaron la fase sp3, estan
    agrupados o dispersos? Responde "los dominios sp2 nuevos vienen de la
    fragmentacion sp3?".
"""
import sys
sys.path.insert(0, "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/src")
import numpy as np
from scipy.spatial import cKDTree
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
from ac_analyzer.core.reader import parse_lammps_data

D = "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/samples_for_analysis/camorfo_sp3_60_mCRA"
CUT, T = 2.064, 3


def frame(cyc):
    f = parse_lammps_data(f"{D}/cycle{cyc}.data")
    L = f.box_lengths
    pos = f.positions - np.floor(f.positions / L) * L
    pairs = cKDTree(pos, boxsize=L).query_pairs(r=CUT, output_type="ndarray")
    n = len(pos)
    cn = np.bincount(pairs.ravel(), minlength=n)
    return f, pos, L, pairs, cn, n


def core_clusters(pairs, cn, n):
    sp3 = cn == 4
    sel = sp3[pairs[:, 0]] & sp3[pairs[:, 1]]
    intdeg = np.bincount(pairs[sel].ravel(), minlength=n)
    mask = sp3 & (intdeg >= T)
    m = mask[pairs[:, 0]] & mask[pairs[:, 1]]
    e, idx = pairs[m], np.where(mask)[0]
    remap = -np.ones(n, dtype=np.int64)
    remap[idx] = np.arange(len(idx))
    g = coo_matrix((np.ones(len(e)), (remap[e[:, 0]], remap[e[:, 1]])), shape=(len(idx), len(idx)))
    ncomp, lab = connected_components(g, directed=False)
    return idx, lab, np.bincount(lab), mask


def fit_tau(sizes, smin=5, smax=None):
    """MLE de Clauset-Shalizi-Newman para ley de potencias discreta."""
    s = sizes[sizes >= smin]
    if smax:
        s = s[s <= smax]
    if len(s) < 50:
        return None, len(s)
    return 1 + len(s) / np.sum(np.log(s / (smin - 0.5))), len(s)


def rg(pos_sub, L):
    ref = pos_sub[0]
    d = pos_sub - ref
    d -= L * np.round(d / L)
    d -= d.mean(axis=0)
    return float(np.sqrt((d ** 2).sum(axis=1).mean()))


print("DISTRIBUCION DE TAMANOS DEL NUCLEO sp3 Y RADIO DE GIRO")
print(f"{'ciclo':>6}{'n clusters':>12}{'tau ajustado':>14}{'n usados':>10}{'Rg mayor (A)':>15}{'2do mayor':>11}")
for c in (0, 20, 30, 35, 40, 60, 100):
    f, pos, L, pairs, cn, n = frame(c)
    idx, lab, sizes, mask = core_clusters(pairs, cn, n)
    tau, nused = fit_tau(sizes)
    big = int(sizes.argmax())
    r = rg(pos[idx[lab == big]], L)
    second = int(np.sort(sizes)[-2]) if len(sizes) > 1 else 0
    print(f"{c:>6}{len(sizes):>12,}{(f'{tau:.3f}' if tau else '-'):>14}{nused:>10,}{r:>15.1f}{second:>11,}")

print("\nCORRELACION ESPACIAL DEL DANO (atomos que dejaron sp3 entre c0 y c100)")
f0, pos0, L0, pairs0, cn0, n0 = frame(0)
f1, pos1, L1, pairs1, cn1, n1 = frame(100)
id0 = {int(a): i for i, a in enumerate(f0.atom_id)}
common = np.array([id0[int(a)] for a in f1.atom_id if int(a) in id0])
sp3_0 = (cn0 == 4)[common]
sp3_1 = (cn1 == 4)
left = sp3_0 & ~sp3_1
print(f"  atomos que eran sp3 en c0 y no lo son en c100: {int(left.sum()):,}")
# fraccion de vecinos (en c100) de un atomo 'left' que tambien son 'left'
sel = pairs1[left[pairs1[:, 0]] & left[pairs1[:, 1]]]
deg_all = np.bincount(pairs1.ravel(), minlength=n1)
deg_left = np.bincount(sel.ravel(), minlength=n1)
obs = deg_left[left].sum() / deg_all[left].sum()
exp = left.sum() / n1
print(f"  fraccion observada de vecinos que tambien dejaron la fase: {obs:.4f}")
print(f"  fraccion esperada si estuvieran distribuidos al azar:      {exp:.4f}")
print(f"  factor de agrupamiento: {obs/exp:.2f}x")
