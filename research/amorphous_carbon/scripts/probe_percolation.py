#!/usr/bin/env python3
"""Donde ocurre la perdida de percolacion del nucleo sp3, y sobrevive a variar
el cutoff de coordinacion?

Nucleo = atomos sp3 con al menos t vecinos sp3. Se reporta la fraccion del
nucleo en su componente conexa mayor, y si esa componente atraviesa la caja en
las tres direcciones (percolacion real, no proxy).
"""
import sys
sys.path.insert(0, "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/src")

import numpy as np
from scipy.spatial import cKDTree
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
from ac_analyzer.core.reader import parse_lammps_data

D = "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/samples_for_analysis/camorfo_sp3_60_mCRA"
CYCLES = [0, 2, 5, 10, 15, 20, 30, 40, 60, 80, 100]
T = 3


def spans(pos_sub, L):
    """INVALIDA -- NO USAR. Se conserva sólo para documentar el error.

    La idea era: la componente atraviesa la caja si su extensión supera L/2 en
    cada eje. Sobre coordenadas envueltas eso es falso: cualquier conjunto
    disperso da extensión > L/2 aunque no esté conectado a través del borde.
    Daba "atraviesa" incluso para el cluster de 3379 átomos del ciclo 100.

    El test correcto está en `percolation_test.py`: BFS arrastrando el
    desplazamiento de imagen periódica; si un ciclo cierra con un offset de red
    no nulo, el cluster se conecta consigo mismo a través de la celda. Es el
    mismo criterio que implementa el modificador en C++.
    """
    return [(pos_sub[:, k].max() - pos_sub[:, k].min()) > 0.5 * L[k] for k in range(3)]


def analyse(cyc, cut, t=T):
    f = parse_lammps_data(f"{D}/cycle{cyc}.data")
    L = f.box_lengths
    pos = f.positions - np.floor(f.positions / L) * L
    pairs = cKDTree(pos, boxsize=L).query_pairs(r=cut, output_type="ndarray")
    n = len(pos)
    cn = np.bincount(pairs.ravel(), minlength=n)
    sp3 = cn == 4
    sel = sp3[pairs[:, 0]] & sp3[pairs[:, 1]]
    intdeg = np.bincount(pairs[sel].ravel(), minlength=n)
    mask = sp3 & (intdeg >= t)
    m = mask[pairs[:, 0]] & mask[pairs[:, 1]]
    e, idx = pairs[m], np.where(mask)[0]
    if len(idx) == 0 or len(e) == 0:
        return dict(n_core=len(idx), lcc=0, frac=0.0, ncl=len(idx), span=[False] * 3)
    remap = -np.ones(n, dtype=np.int64)
    remap[idx] = np.arange(len(idx))
    g = coo_matrix((np.ones(len(e)), (remap[e[:, 0]], remap[e[:, 1]])), shape=(len(idx), len(idx)))
    ncomp, lab = connected_components(g, directed=False)
    sizes = np.bincount(lab)
    big = int(sizes.argmax())
    members = idx[lab == big]
    return dict(n_core=len(idx), lcc=int(sizes.max()), frac=sizes.max() / len(idx),
                ncl=int(ncomp), span=spans(pos[members], L))


print("PERDIDA DE PERCOLACION DEL NUCLEO sp3 (t>=3), cutoff 2.064")
print(f"{'ciclo':>6}{'atomos nucleo':>15}{'cluster mayor':>15}{'fraccion':>11}{'n clusters':>12}{'atraviesa xyz':>16}")
for c in CYCLES:
    r = analyse(c, 2.064)
    print(f"{c:>6}{r['n_core']:>15,}{r['lcc']:>15,}{r['frac']:>11.4f}{r['ncl']:>12,}"
          f"{'  '+''.join('X' if s else '-' for s in r['span']):>16}")

print("\nROBUSTEZ AL CUTOFF DE COORDINACION (t>=3)")
print(f"{'cutoff':>8}{'ciclo':>7}{'atomos nucleo':>15}{'fraccion mayor':>16}{'n clusters':>12}")
for cut in (1.95, 2.064, 2.15):
    for c in (0, 100):
        r = analyse(c, cut)
        print(f"{cut:>8}{c:>7}{r['n_core']:>15,}{r['frac']:>16.4f}{r['ncl']:>12,}")
