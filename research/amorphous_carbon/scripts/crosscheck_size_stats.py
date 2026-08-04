#!/usr/bin/env python3
"""Cross-check the remaining AmorphousCarbonModifier metrics ported from the
reference Python pipeline (ac_analyzer.analysis.sp2_clusters.sp2_cluster_metrics)
on 2026-08-01: mean/median/p90 of cluster size, cluster number density, and the
Robertson-relation gap estimate.

Compares, per cycle:
    AmorphousCarbon.mean_cluster_size       vs  mean_size
    AmorphousCarbon.median_cluster_size     vs  median_size
    AmorphousCarbon.cluster_size_p90        vs  size_p90
    AmorphousCarbon.cluster_number_density  vs  cluster_number_density
    AmorphousCarbon.gap_estimate_eV         vs  gap_estimate_eV

All five are computed over EVERY connected component of the target phase (not
filtered by the relevance threshold), matching the Python reference's scope --
so, unlike crosscheck_carbon.py, this script calls sp2_cluster_metrics with
min_cluster_size=1 (the Python default), since a stricter value only affects
n_clusters_min/clustered_fraction, not the five metrics checked here.

Usage:
    python3 crosscheck_size_stats.py <csv_from_--carbon-track> <cycles_dir> <sp2|sp3> [cycles...]
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
BOND_CUTOFF = 2.064   # Equal to the coordination cutoff by default; see AMORPHOUS_CARBON_MODIFIER.md.


def load_csv(path):
    with open(path) as fh:
        return {int(row["source_frame"]): row for row in csv.DictReader(fh)}


def main():
    csv_path, cycles_dir, species = sys.argv[1], Path(sys.argv[2]), sys.argv[3]
    cycles = [int(c) for c in sys.argv[4:]] or [0, 50, 100]
    target_coord = 3 if species == "sp2" else 4

    rows = load_csv(csv_path)
    print(f"{'cycle':>6} {'quantity':>26} {'cpp':>16} {'python':>16} {'match':>7}")
    all_ok = True
    for cyc in cycles:
        f = cycles_dir / f"cycle{cyc}.data"
        frame = parse_lammps_data(f)
        coord = compute_coordination(frame.positions, frame.box_lengths, COORD_CUTOFF)
        mask = np.asarray(coord) == target_coord
        m = sp2_cluster_metrics(frame.positions, frame.box_lengths, mask, bond_cutoff=BOND_CUTOFF)
        row = rows.get(cyc)
        if row is None:
            print(f"{cyc:>6}  (no row in the CSV)")
            all_ok = False
            continue

        checks = [
            ("mean_cluster_size", float(row["mean_cluster_size"]), float(m["mean_size"])),
            ("median_cluster_size", float(row["median_cluster_size"]), float(m["median_size"])),
            ("cluster_size_p90", float(row["cluster_size_p90"]), float(m["size_p90"])),
            ("cluster_number_density", float(row["cluster_number_density"]), float(m["cluster_number_density"])),
            ("gap_estimate_eV", float(row["gap_estimate_eV"]), float(m["gap_estimate_eV"])),
        ]
        for name, cpp, py in checks:
            tol = 1e-6 * max(1.0, abs(py))
            ok = abs(cpp - py) <= tol
            all_ok &= ok
            print(f"{cyc:>6} {name:>26} {cpp:>16.10g} {py:>16.10g} {'OK' if ok else 'DIFF':>7}")

    print("\nRESULT:", "all match" if all_ok else "MISMATCH")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
