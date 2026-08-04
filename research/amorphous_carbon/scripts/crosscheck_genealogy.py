#!/usr/bin/env python3
"""Independent reference implementation of the bidirectional cluster
classification (Stable/Fragmented/Merged/Created/Disappeared) in
AmorphousCarbonTrackingModifier ("Reference Cluster Genealogy" / "Cluster
Genealogy" particle properties and the AmorphousCarbonTracking.reference_
clusters_* / current_clusters_* attributes).

Algorithm: build the atom-overlap bipartite graph between "relevant" (size >=
minClusterSize) reference clusters and "relevant" current clusters, restricted
to atoms matched by id between the two frames. An edge (R, C) exists when
overlap(R,C) >= tau * size(R)  OR  overlap(R,C) >= tau * size(C).

Then label each side independently by its own degree in that graph:
  reference cluster R: outdeg 0 -> Disappeared, 1 -> Stable, >=2 -> Fragmented
  current cluster   C: indeg  0 -> Created,     1 -> Stable, >=2 -> Merged

This is the standard overlap-tracking scheme used for storm-cell / grain
tracking (e.g. TITAN, Dixon & Wiener 1993): each side's label depends only on
its own edges, so a ref cluster can be "Stable" (single continuation) while
its target is "Merged" (it also absorbed another ref cluster) -- that
asymmetry is the point of a *bidirectional* classification.

Validated bit-for-bit (2026-08-01) against the native `--carbon-track` batch
mode at the default threshold (tau=0.10) on camorfo_sp3_60_mCRA: sp3 cycle
0->10 (199/2/13 stable/fragmented/disappeared, 450/4/12 stable/merged/created),
sp3 cycle 0->100 (194/5/15, 1107/8/34) and sp2 cycle 0->100 (3370/573/6200,
3723/406/14015) all match exactly.
"""
import sys
sys.path.insert(0, "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/src")

import numpy as np
from scipy.spatial import cKDTree
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
from ac_analyzer.core.reader import parse_lammps_data

CUT = 2.064
MIN_RELEVANT = 3
D = "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/samples_for_analysis/camorfo_sp3_60_mCRA"


def load_clusters(cyc, species="sp3"):
    f = parse_lammps_data(f"{D}/cycle{cyc}.data")
    L = f.box_lengths
    pos = f.positions - np.floor(f.positions / L) * L
    pairs = cKDTree(pos, boxsize=L).query_pairs(r=CUT, output_type="ndarray")
    n = len(pos)
    cn = np.bincount(pairs.ravel(), minlength=n)
    mask = (cn == 4) if species == "sp3" else (cn == 3)
    sel = mask[pairs[:, 0]] & mask[pairs[:, 1]]
    e = pairs[sel]
    idx = np.where(mask)[0]
    remap = -np.ones(n, dtype=np.int64)
    remap[idx] = np.arange(len(idx))
    if len(e) > 0:
        g = coo_matrix((np.ones(len(e)), (remap[e[:, 0]], remap[e[:, 1]])), shape=(len(idx), len(idx)))
        ncomp, lab = connected_components(g, directed=False)
    else:
        ncomp, lab = len(idx), np.arange(len(idx))
    sizes = np.bincount(lab, minlength=ncomp)
    # cluster id per atom (0 = not in tracked phase), 1-indexed like the C++ side
    cid = np.zeros(n, dtype=np.int64)
    cid[idx] = lab + 1
    csize = np.zeros(n, dtype=np.int64)
    csize[idx] = sizes[lab]
    return f.atom_id, cid, csize, ncomp, sizes


def classify(ref_cyc, cur_cyc, species, tau, min_relevant=MIN_RELEVANT, verbose=True):
    ref_id, ref_cid, ref_csize, ref_ncomp, ref_sizes = load_clusters(ref_cyc, species)
    cur_id, cur_cid, cur_csize, cur_ncomp, cur_sizes = load_clusters(cur_cyc, species)

    ref_index = {aid: i for i, aid in enumerate(ref_id)}
    # match atoms by id: for every current atom, find its ref-frame row (if any)
    matched_ref_row = np.full(len(cur_id), -1, dtype=np.int64)
    for j, aid in enumerate(cur_id):
        i = ref_index.get(aid, -1)
        if i >= 0:
            matched_ref_row[j] = i

    have_match = matched_ref_row >= 0
    r_cid_matched = np.where(have_match, ref_cid[matched_ref_row], 0)
    c_cid_matched = np.where(have_match, cur_cid, 0)

    relevant_ref = np.zeros(ref_ncomp + 1, dtype=bool)
    relevant_ref[1:] = ref_sizes >= min_relevant
    relevant_cur = np.zeros(cur_ncomp + 1, dtype=bool)
    relevant_cur[1:] = cur_sizes >= min_relevant

    sel = have_match & relevant_ref[r_cid_matched] & relevant_cur[c_cid_matched]
    rr = r_cid_matched[sel]
    cc = c_cid_matched[sel]

    # overlap matrix as a dict keyed (r,c) -> count
    from collections import defaultdict
    overlap = defaultdict(int)
    for r, c in zip(rr, cc):
        overlap[(r, c)] += 1

    out_edges = defaultdict(list)   # r -> list of (c, overlap)
    in_edges = defaultdict(list)    # c -> list of (r, overlap)
    for (r, c), ov in overlap.items():
        sr, sc = ref_sizes[r - 1], cur_sizes[c - 1]
        if ov >= tau * sr or ov >= tau * sc:
            out_edges[r].append((c, ov))
            in_edges[c].append((r, ov))

    relevant_ref_ids = [r for r in range(1, ref_ncomp + 1) if relevant_ref[r]]
    relevant_cur_ids = [c for c in range(1, cur_ncomp + 1) if relevant_cur[c]]

    ref_label = {}
    for r in relevant_ref_ids:
        d = len(out_edges.get(r, []))
        ref_label[r] = "Disappeared" if d == 0 else ("Stable" if d == 1 else "Fragmented")

    cur_label = {}
    for c in relevant_cur_ids:
        d = len(in_edges.get(c, []))
        cur_label[c] = "Created" if d == 0 else ("Stable" if d == 1 else "Merged")

    from collections import Counter
    ref_counts = Counter(ref_label.values())
    cur_counts = Counter(cur_label.values())

    if verbose:
        print(f"\n=== {species} cycle {ref_cyc} -> {cur_cyc}, tau={tau} ===")
        print(f"relevant ref clusters: {len(relevant_ref_ids)}  -> {dict(ref_counts)}")
        print(f"relevant cur clusters: {len(relevant_cur_ids)}  -> {dict(cur_counts)}")
        # rank-1 (largest) reference cluster sanity check
        r1 = max(relevant_ref_ids, key=lambda r: ref_sizes[r - 1]) if relevant_ref_ids else None
        if r1 is not None:
            edges = sorted(out_edges.get(r1, []), key=lambda t: -t[1])
            print(f"rank-1 ref cluster (size {ref_sizes[r1-1]}): label={ref_label[r1]}, "
                  f"edges={[(c, ov, f'{ov/ref_sizes[r1-1]:.3f}') for c, ov in edges[:5]]}")
        c1 = max(relevant_cur_ids, key=lambda c: cur_sizes[c - 1]) if relevant_cur_ids else None
        if c1 is not None:
            edges = sorted(in_edges.get(c1, []), key=lambda t: -t[1])
            print(f"rank-1 cur cluster (size {cur_sizes[c1-1]}): label={cur_label[c1]}, "
                  f"edges={[(r, ov, f'{ov/cur_sizes[c1-1]:.3f}') for r, ov in edges[:5]]}")
    return ref_label, cur_label, ref_sizes, cur_sizes


def _counts(label_dict, keys):
    from collections import Counter
    c = Counter(label_dict.values())
    return tuple(c.get(k, 0) for k in keys)


# (species, ref_cyc, cur_cyc) -> expected (ref: stable, fragmented, disappeared),
# (cur: stable, merged, created), read from --carbon-track-genealogy-threshold 0.10
# CSV output on 2026-08-01 (see AMORPHOUS_CARBON_MODIFIER.md, "Bidirectional cluster genealogy").
EXPECTED = {
    ("sp3", 0, 10):  ((199, 2, 13),   (450, 4, 12)),
    ("sp3", 0, 100):  ((194, 5, 15),  (1107, 8, 34)),
    ("sp2", 0, 100):  ((3370, 573, 6200), (3723, 406, 14015)),
}

if __name__ == "__main__":
    print("=== Regression check against the native modifier (tau=0.10) ===")
    all_ok = True
    for (species, ref_cyc, cur_cyc), (expected_ref, expected_cur) in EXPECTED.items():
        ref_label, cur_label, _, _ = classify(ref_cyc, cur_cyc, species, 0.10, verbose=False)
        got_ref = _counts(ref_label, ("Stable", "Fragmented", "Disappeared"))
        got_cur = _counts(cur_label, ("Stable", "Merged", "Created"))
        ok = (got_ref == expected_ref) and (got_cur == expected_cur)
        all_ok &= ok
        print(f"{species} {ref_cyc}->{cur_cyc}: ref {got_ref} (expected {expected_ref}), "
              f"cur {got_cur} (expected {expected_cur})  {'OK' if ok else 'MISMATCH'}")
    print("ALL OK" if all_ok else "MISMATCH DETECTED")

    print("\n=== Threshold sensitivity (should be nearly unchanged across 0.05-0.30) ===")
    for tau in (0.05, 0.10, 0.20, 0.30):
        classify(0, 100, "sp3", tau)
    print()
    for tau in (0.05, 0.10, 0.20):
        classify(0, 10, "sp3", tau)
    print()
    for tau in (0.05, 0.10, 0.20):
        classify(0, 100, "sp2", tau)
