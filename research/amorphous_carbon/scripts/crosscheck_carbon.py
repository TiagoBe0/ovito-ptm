#!/usr/bin/env python3
"""Cross-check del AmorphousCarbonModifier nativo (C++) contra el pipeline Python
de referencia (ac_analyzer.analysis.sp2_clusters) sobre las mismas muestras.

Compara, por ciclo:
    AmorphousCarbon.sp3_count            vs  (coordination == 4).sum()
    AmorphousCarbon.cluster_count        vs  n_clusters
    AmorphousCarbon.largest_cluster_size vs  largest_size
    AmorphousCarbon.clustered_fraction   vs  clustered_fraction (min_cluster_size=2)

Uso:
    python3 crosscheck_carbon.py <csv_del_sweep> <dir_de_ciclos> <sp2|sp3> [ciclos...]
"""
import sys
import csv
from pathlib import Path

sys.path.insert(0, "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/src")

import numpy as np
from ac_analyzer.core.reader import parse_lammps_data
from ac_analyzer.analysis.coordination import compute_coordination
from ac_analyzer.analysis.sp2_clusters import sp2_cluster_metrics

COORD_CUTOFF = 2.064
# Igual al de coordinacion, siguiendo el default corregido del modificador nativo:
# un enlace que alcanza para hacer sp3 a un atomo tambien debe conectar su cluster.
BOND_CUTOFF = 2.064


def load_csv(path):
    with open(path) as fh:
        return {int(row["source_frame"]): row for row in csv.DictReader(fh)}


def main():
    csv_path, cycles_dir, species = sys.argv[1], Path(sys.argv[2]), sys.argv[3]
    cycles = [int(c) for c in sys.argv[4:]] or [0, 50, 100]
    target_coord = 3 if species == "sp2" else 4

    rows = load_csv(csv_path)
    # El sweep indexa por source_frame; OVITO ordena la secuencia numerica cycle0..cycle100,
    # asi que source_frame N == cycleN. Eso se verifica abajo comparando conteos absolutos.
    print(f"{'cycle':>6} {'quantity':>26} {'cpp':>14} {'python':>14} {'match':>7}")
    all_ok = True
    for cyc in cycles:
        f = cycles_dir / f"cycle{cyc}.data"
        frame = parse_lammps_data(f)
        coord = compute_coordination(frame.positions, frame.box_lengths, COORD_CUTOFF)
        mask = np.asarray(coord) == target_coord
        m = sp2_cluster_metrics(frame.positions, frame.box_lengths, mask,
                                bond_cutoff=BOND_CUTOFF, min_cluster_size=2)
        row = rows.get(cyc)
        if row is None:
            print(f"{cyc:>6}  (sin fila en el CSV)")
            all_ok = False
            continue

        checks = [
            (f"{species}_count", int(row[f"{species}_count"]), int(mask.sum()), 0),
            ("cluster_count", int(row["cluster_count"]), int(m["n_clusters"]), 0),
            ("largest_cluster_size", int(row["largest_cluster_size"]), int(m["largest_size"]), 0),
            ("clustered_fraction", float(row["clustered_fraction"]), float(m["clustered_fraction"]), 1e-6),
        ]
        for name, cpp, py, tol in checks:
            ok = abs(cpp - py) <= tol
            all_ok &= ok
            print(f"{cyc:>6} {name:>26} {cpp:>14} {py:>14} {'OK' if ok else 'DIFF':>7}")

    print("\nRESULTADO:", "todos coinciden" if all_ok else "HAY DIFERENCIAS")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
