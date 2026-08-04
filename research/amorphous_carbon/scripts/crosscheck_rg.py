#!/usr/bin/env python3
"""Cross-check AmorphousCarbon.largest_cluster_radius_of_gyration and
AmorphousCarbon.second_largest_cluster_size against an independent Python
implementation, on the sp3 core (>= T same-phase neighbors).

The C++ modifier unwraps each cluster along its BFS tree, accumulating the real
periodic displacement between bonded neighbors. This script reproduces that
definition with a BFS of its own, and also reports the cruder definition used by
the earlier probe_metrics2.py (minimum image relative to one arbitrary atom of
the cluster) so the two can be compared.

Note that neither definition is a cluster size while the cluster percolates: a
spanning cluster touches its own periodic image, so no unwrapping of it is
well-defined. Both numbers then saturate at the scale of the cell.

Usage: crosscheck_rg.py <csv_from_--carbon-track> <cycles_dir> [cycles...]
"""
import csv
import sys
from collections import deque

import numpy as np
from scipy.spatial import cKDTree

sys.path.insert(0, "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/src")
from ac_analyzer.core.reader import parse_lammps_data  # noqa: E402

CUT = 2.064
T = 3


def load(path):
    f = parse_lammps_data(path)
    L = f.box_lengths
    pos = f.positions - np.floor(f.positions / L) * L
    pairs = cKDTree(pos, boxsize=L).query_pairs(r=CUT, output_type="ndarray")
    return pos, L, pairs, len(pos)


def core_mask(pairs, n):
    cn = np.bincount(pairs.ravel(), minlength=n)
    sp3 = cn == 4
    both = sp3[pairs[:, 0]] & sp3[pairs[:, 1]]
    intdeg = np.bincount(pairs[both].ravel(), minlength=n)
    return sp3 & (intdeg >= T)


def adjacency(pairs, mask, n):
    keep = pairs[mask[pairs[:, 0]] & mask[pairs[:, 1]]]
    adj = [[] for _ in range(n)]
    for a, b in keep:
        adj[a].append(b)
        adj[b].append(a)
    return adj


def bfs_clusters(adj, mask, n, pos, L):
    """Connected components plus BFS-unwrapped coordinates, the C++ definition."""
    label = np.full(n, -1, dtype=np.int64)
    unwrapped = np.zeros((n, 3))
    sizes = []
    for seed in range(n):
        if not mask[seed] or label[seed] != -1:
            continue
        cid = len(sizes)
        label[seed] = cid
        unwrapped[seed] = 0.0
        q = deque([seed])
        count = 0
        while q:
            cur = q.popleft()
            count += 1
            for nb in adj[cur]:
                if label[nb] == -1:
                    d = pos[nb] - pos[cur]
                    d -= L * np.round(d / L)  # real periodic displacement, as q.delta()
                    label[nb] = cid
                    unwrapped[nb] = unwrapped[cur] + d
                    q.append(nb)
        sizes.append(count)
    return label, unwrapped, np.array(sizes)


def rg_unwrapped(coords):
    d = coords - coords.mean(axis=0)
    return float(np.sqrt((d ** 2).sum(axis=1).mean()))


def rg_minimage(pos_sub, L):
    """The earlier probe's definition: minimum image relative to one atom."""
    d = pos_sub - pos_sub[0]
    d -= L * np.round(d / L)
    d -= d.mean(axis=0)
    return float(np.sqrt((d ** 2).sum(axis=1).mean()))


def main():
    csv_path, cycles_dir = sys.argv[1], sys.argv[2]
    cycles = [int(c) for c in sys.argv[3:]] or [0, 20, 30, 35, 40, 60, 100]

    rows = {int(r["source_frame"]): r for r in csv.DictReader(open(csv_path))}

    hdr = (f"{'cyc':>4} {'2nd C++':>9} {'2nd py':>9} {'ok':>3} "
           f"{'Rg C++':>9} {'Rg py':>9} {'diff':>8} {'Rg minimg':>10} {'perc':>5}")
    print(hdr)
    print("-" * len(hdr))
    failures = 0
    for c in cycles:
        pos, L, pairs, n = load(f"{cycles_dir}/cycle{c}.data")
        mask = core_mask(pairs, n)
        adj = adjacency(pairs, mask, n)
        label, unwrapped, sizes = bfs_clusters(adj, mask, n, pos, L)

        order = np.argsort(-sizes, kind="stable")
        largest = order[0]
        second_py = int(sizes[order[1]]) if len(sizes) >= 2 else 0
        members = label == largest
        rg_py = rg_unwrapped(unwrapped[members])
        rg_mi = rg_minimage(pos[members], L)

        r = rows[c]
        second_cpp = int(r["second_largest_cluster_size"])
        rg_cpp = float(r["largest_cluster_radius_of_gyration"])
        percolates = r["largest_cluster_percolates"] == "1"
        ok = second_cpp == second_py
        diff = abs(rg_cpp - rg_py)
        # Where the cluster does not span the cell, the unwrapping is unique up to a global
        # translation, so two independent BFS orders must agree to machine precision. Where it
        # does span, atoms reachable by two paths differing by a cell vector take whichever
        # image their BFS reached first, and a handful of atoms landing differently moves Rg
        # slightly. That is a property of the quantity, not a defect of either implementation --
        # and it is one more reason not to read Rg as a size while percolates == 1.
        tol = 1.0 if percolates else 1e-9
        if not ok or diff > tol:
            failures += 1
        print(f"{c:>4} {second_cpp:>9,} {second_py:>9,} {'yes' if ok else 'NO':>3} "
              f"{rg_cpp:>9.4f} {rg_py:>9.4f} {diff:>8.2e} {rg_mi:>10.1f} "
              f"{r['largest_cluster_percolates']:>5}")

    print()
    print("Tolerance: exact (1e-9) where the cluster does not percolate, 1.0 A where it does.")
    print("MISMATCHES:" if failures else "all cycles agree", failures or "")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
