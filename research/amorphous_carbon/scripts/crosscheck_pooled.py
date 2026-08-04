#!/usr/bin/env python3
"""Independent reference implementation of the pooled cluster-fate aggregates in
AmorphousCarbonTrackingModifier (the AmorphousCarbonTracking.relevant_* global
attributes / CSV columns).

Why they exist: the older AmorphousCarbonTracking.retained_fraction and friends
describe only the rank-1 (largest) reference cluster. That is the whole story
for a percolating sp3 matrix, but for sp2 on camorfo_sp3_60_mCRA the rank-1
cluster holds 14 atoms out of 174096, so those numbers are noise. The relevant_*
attributes pool the same fate breakdown over *every* reference cluster at or
above the upstream "Minimum relevant cluster size".

Algorithm (mirrors the C++ but written from the paper description, not the code):
match atoms by id; for every current atom whose reference cluster is relevant,
classify it as retained / left-phase / left-core / dispersed exactly as the
rank-1 path does, then run the same per-reference-cluster majority vote to find
each cluster's main fragment and count how many distinct current clusters it
scattered into. Denominator is the number of reference atoms in relevant
reference clusters, so retained + left_phase + left_core + vanished == 1.

Note this script assumes minSamePhaseNeighbors == 0 (the default), where an atom
in the tracked phase always carries a cluster id, so cluster id 0 means the atom
left the phase and left_core is identically 0.

Usage:
    python3 research/amorphous_carbon/scripts/crosscheck_pooled.py
"""
import sys
from collections import defaultdict

import numpy as np

from crosscheck_genealogy import load_clusters, MIN_RELEVANT


def pooled_metrics(ref_cyc, cur_cyc, species, min_relevant=MIN_RELEVANT):
    ref_id, ref_cid, ref_csize, ref_ncomp, ref_sizes = load_clusters(ref_cyc, species)
    cur_id, cur_cid, cur_csize, cur_ncomp, cur_sizes = load_clusters(cur_cyc, species)

    ref_index = {aid: i for i, aid in enumerate(ref_id)}
    relevant_ref = np.zeros(ref_ncomp + 1, dtype=bool)
    relevant_ref[1:] = ref_sizes >= min_relevant

    # Denominator: reference atoms sitting in a relevant reference cluster.
    relevant_ref_atoms = int(relevant_ref[ref_cid].sum())
    n_relevant_ref_clusters = int(relevant_ref[1:].sum())

    retained = left_phase = left_core = dispersed = 0
    pairs = []          # (refCid, curCid) for atoms still in a cluster of size >= 2
    pair_relevant = []  # whether that atom's reference cluster is relevant
    seen_ref_rows = set()

    for j, aid in enumerate(cur_id):
        i = ref_index.get(aid, -1)
        if i < 0:
            continue    # unmapped: present now, absent from the reference
        seen_ref_rows.add(i)
        r = ref_cid[i]
        if r == 0:
            continue    # not in a tracked-phase cluster in the reference
        is_relevant = bool(relevant_ref[r])
        c = cur_cid[j]
        if c == 0:
            # With no core filter, cluster id 0 can only mean the atom left the phase.
            if is_relevant:
                left_phase += 1
            continue
        if is_relevant:
            retained += 1
        if cur_csize[j] == 1:
            if is_relevant:
                dispersed += 1
            continue
        pairs.append((r, c))
        pair_relevant.append(is_relevant)

    vanished = sum(1 for i, r in enumerate(ref_cid)
                   if r != 0 and relevant_ref[r] and i not in seen_ref_rows)

    # Majority vote per reference cluster: the current cluster inheriting the most
    # atoms is the main fragment; ties go to the smallest current cluster id.
    counts = defaultdict(lambda: defaultdict(int))
    for r, c in pairs:
        counts[r][c] += 1
    main_fragment = {}
    fragment_count = {}
    for r, dest in counts.items():
        best_c, best_n = None, 0
        for c in sorted(dest):
            if dest[c] > best_n:
                best_n, best_c = dest[c], c
        main_fragment[r] = best_c
        fragment_count[r] = len(dest)

    main_fragment_atoms = sum(
        1 for (r, c), rel in zip(pairs, pair_relevant)
        if rel and c == main_fragment[r]
    )
    relevant_fragment_count = sum(n for r, n in fragment_count.items() if relevant_ref[r])

    denom = float(relevant_ref_atoms)
    return {
        "relevant_reference_atom_count": relevant_ref_atoms,
        "relevant_main_fragment_size": main_fragment_atoms,
        "relevant_main_fragment_fraction": main_fragment_atoms / denom,
        "relevant_retained_fraction": retained / denom,
        "relevant_left_phase_fraction": left_phase / denom,
        "relevant_left_core_fraction": left_core / denom,
        "relevant_dispersed_fraction": dispersed / denom,
        "relevant_vanished_fraction": vanished / denom,
        "relevant_fragment_count": relevant_fragment_count,
        "relevant_mean_fragments_per_cluster": relevant_fragment_count / n_relevant_ref_clusters,
    }


# Read from `--carbon-track` CSV output on 2026-08-02 (see AMORPHOUS_CARBON_MODIFIER.md,
# "Pooled aggregates over all relevant clusters"). Integers are exact; fractions are
# compared to 1e-12.
EXPECTED = {
    ("sp3", 0, 10): {
        "relevant_reference_atom_count": 294703,
        "relevant_main_fragment_size": 247767,
        "relevant_main_fragment_fraction": 0.8407345700586693,
        "relevant_retained_fraction": 0.8579620838607004,
        "relevant_left_phase_fraction": 0.1420379161392996,
        "relevant_left_core_fraction": 0.0,
        "relevant_dispersed_fraction": 0.008123432744152588,
        "relevant_vanished_fraction": 0.0,
        "relevant_fragment_count": 1273,
        "relevant_mean_fragments_per_cluster": 5.948598130841122,
    },
    ("sp3", 0, 100): {
        "relevant_reference_atom_count": 294703,
        "relevant_main_fragment_size": 160792,
        "relevant_main_fragment_fraction": 0.5456069330817807,
        "relevant_retained_fraction": 0.5772150266539533,
        "relevant_left_phase_fraction": 0.4227849733460467,
        "relevant_left_core_fraction": 0.0,
        "relevant_dispersed_fraction": 0.01300970807898121,
        "relevant_vanished_fraction": 0.0,
        "relevant_fragment_count": 2746,
        "relevant_mean_fragments_per_cluster": 12.83177570093458,
    },
    ("sp2", 0, 100): {
        "relevant_reference_atom_count": 35090,
        "relevant_main_fragment_size": 8273,
        "relevant_main_fragment_fraction": 0.2357651752636079,
        "relevant_retained_fraction": 0.4553434026788259,
        "relevant_left_phase_fraction": 0.5446565973211741,
        "relevant_left_core_fraction": 0.0,
        "relevant_dispersed_fraction": 0.1674836135651183,
        "relevant_vanished_fraction": 0.0,
        "relevant_fragment_count": 8090,
        "relevant_mean_fragments_per_cluster": 0.7975944000788722,
    },
}

if __name__ == "__main__":
    all_ok = True
    for (species, ref_cyc, cur_cyc), expected in EXPECTED.items():
        print(f"\n=== {species} cycle {ref_cyc} -> {cur_cyc} ===")
        got = pooled_metrics(ref_cyc, cur_cyc, species)
        for key, exp in expected.items():
            val = got[key]
            ok = (val == exp) if isinstance(exp, int) else (abs(val - exp) <= 1e-12)
            all_ok &= ok
            print(f"  {'OK      ' if ok else 'MISMATCH'} {key:38s} got {val!r:24s} expected {exp!r}")
    print("\nALL OK" if all_ok else "\nMISMATCH DETECTED")
    sys.exit(0 if all_ok else 1)
