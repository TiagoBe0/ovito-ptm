#!/usr/bin/env python3
"""Distribucion de longitudes de enlace C-C y cuantificacion de la
inconsistencia entre coordination_cutoff (2.064) y cluster bond_cutoff (1.85)."""
import sys
sys.path.insert(0, "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/src")

import numpy as np
from scipy.spatial import cKDTree
from ac_analyzer.core.reader import parse_lammps_data

COORD = 2.064
BOND = 1.85
D = "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/samples_for_analysis/camorfo_sp3_60_mCRA"

for cyc in (0, 100):
    frame = parse_lammps_data(f"{D}/cycle{cyc}.data")
    pos = frame.positions
    L = frame.box_lengths
    # Envolver a [0,L) para que boxsize de cKDTree aplique PBC.
    wrapped = pos - np.floor(pos / L) * L
    tree = cKDTree(wrapped, boxsize=L)

    pairs = tree.query_pairs(r=COORD, output_type="ndarray")
    d = np.linalg.norm(
        (wrapped[pairs[:, 0]] - wrapped[pairs[:, 1]] + L / 2) % L - L / 2, axis=1)

    n_all = len(d)
    n_stretched = int((d > BOND).sum())
    print(f"\n===== ciclo {cyc} =====")
    print(f"pares dentro de coord_cutoff {COORD}:      {n_all}")
    print(f"  de esos, con longitud > {BOND} (invisibles para conectividad):")
    print(f"  {n_stretched}  ({100*n_stretched/n_all:.1f}% de todos los enlaces)")

    # Histograma para localizar el verdadero primer minimo de la distribucion.
    hist, edges = np.histogram(d, bins=np.arange(1.0, 2.30, 0.025))
    centers = 0.5 * (edges[:-1] + edges[1:])
    print("\n  distribucion de longitudes (conteo por bin de 0.025 A):")
    for c, h in zip(centers, hist):
        if c < 1.3:
            continue
        bar = "#" * int(60 * h / hist.max())
        mark = ""
        if abs(c - BOND) < 0.0125:
            mark = "  <- bond_cutoff 1.85"
        if abs(c - COORD) < 0.0125:
            mark = "  <- coord_cutoff 2.064"
        print(f"   {c:.3f} {h:>8} {bar}{mark}")

    # Cuantos atomos sp3 pierden al menos un vecino al pasar a bond_cutoff?
    cn = np.bincount(pairs.ravel(), minlength=len(pos))
    sp3 = cn == 4
    short = pairs[d <= BOND]
    cn_short = np.bincount(short.ravel(), minlength=len(pos))
    lost = sp3 & (cn_short < 4)
    print(f"\n  atomos sp3: {int(sp3.sum())}")
    print(f"  sp3 que pierden >=1 vecino al usar bond_cutoff: {int(lost.sum())}"
          f"  ({100*lost.sum()/max(sp3.sum(),1):.1f}%)")
    print(f"  sp3 que quedan totalmente aislados con bond_cutoff: "
          f"{int((sp3 & (cn_short == 0)).sum())}")
