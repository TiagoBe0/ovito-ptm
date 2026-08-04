#!/usr/bin/env python3
"""Test de percolacion REAL del nucleo sp3: BFS desenvolviendo posiciones; si un
ciclo cierra con un offset de red no nulo, el cluster se conecta con su propia
imagen periodica. Reemplaza el proxy de extension, que era invalido."""
import sys
sys.path.insert(0, "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/src")
import numpy as np
from scipy.spatial import cKDTree
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
from ac_analyzer.core.reader import parse_lammps_data

D = "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/samples_for_analysis/camorfo_sp3_60_mCRA"

def percolates(pos, L, e, members):
    """e: aristas en indices globales; members: set de indices globales."""
    adj = {}
    for i, j in e:
        i, j = int(i), int(j)
        d = pos[j] - pos[i]
        mic = d - L * np.round(d / L)
        adj.setdefault(i, []).append((j, mic))
        adj.setdefault(j, []).append((i, -mic))
    start = next(iter(members))
    unw = {start: np.zeros(3)}
    stack = [start]
    while stack:
        u = stack.pop()
        for v, mic in adj.get(u, ()):
            npos = unw[u] + mic
            if v not in unw:
                unw[v] = npos; stack.append(v)
            elif np.any(np.abs(npos - unw[v]) > 0.5 * L):
                return True
    return False

def run(cyc, cut=2.064, t=3):
    f = parse_lammps_data(f"{D}/cycle{cyc}.data")
    L = f.box_lengths
    pos = f.positions - np.floor(f.positions / L) * L
    pairs = cKDTree(pos, boxsize=L).query_pairs(r=cut, output_type="ndarray")
    n = len(pos)
    cn = np.bincount(pairs.ravel(), minlength=n)
    sp3 = cn == 4
    sel = sp3[pairs[:,0]] & sp3[pairs[:,1]]
    intdeg = np.bincount(pairs[sel].ravel(), minlength=n)
    res = {}
    for label, mask in (('todo sp3', sp3), (f'nucleo t>={t}', sp3 & (intdeg >= t))):
        m = mask[pairs[:,0]] & mask[pairs[:,1]]
        e, idx = pairs[m], np.where(mask)[0]
        remap = -np.ones(n, dtype=np.int64); remap[idx] = np.arange(len(idx))
        g = coo_matrix((np.ones(len(e)), (remap[e[:,0]], remap[e[:,1]])), shape=(len(idx), len(idx)))
        ncomp, lab = connected_components(g, directed=False)
        sizes = np.bincount(lab); big = int(sizes.argmax())
        members = set(int(x) for x in idx[lab == big])
        esub = e[np.isin(e[:,0], list(members))]
        res[label] = (int(sizes.max()), sizes.max()/len(idx), percolates(pos, L, esub, members))
    return res

print(f"{'ciclo':>6}  {'conjunto':<14}{'cluster mayor':>15}{'fraccion':>11}{'percola?':>11}")
for c in (0, 20, 30, 40, 100):
    r = run(c)
    for k, (sz, fr, p) in r.items():
        print(f"{c:>6}  {k:<14}{sz:>15,}{fr:>11.4f}{('SI' if p else 'NO'):>11}")
