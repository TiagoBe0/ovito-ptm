# Amorphous carbon cluster analysis

Native OVITO modifier for inspecting amorphous-carbon configurations one frame
at a time. It appears in the **Analysis** modifier category as
**Amorphous carbon cluster analysis**.

## Parameters

- **Coordination cutoff**: C-C neighbor cutoff used to determine coordination
  and coordination-based hybridization. The default is 2.064 angstrom.
- **Cluster bond cutoff**: C-C cutoff used to connect atoms of the selected
  hybridization state. The default is 2.064 angstrom — the same as the
  coordination cutoff, so a bond that is good enough to make an atom sp3 is
  also a bond along which its cluster propagates. Shorter values are allowed
  but raise a warning; see "Why the two cutoffs match by default".
- **Minimum relevant cluster size**: clusters below this size remain labeled,
  but are excluded from the relevant-cluster count and clustered fraction.
- **Connected domains**: choose whether connected sp2 or sp3 atoms form the
  clusters.
- **Minimum same-phase neighbors**: cluster only atoms having at least this many
  neighbors of their own phase. 0 clusters the whole phase; 3 clusters its core.
  See "Clustering a percolating network".
- **Detect closed rings (SP/primitive)**: finds closed primitive rings in the
  same-phase bond graph. Off by default (it costs more than clustering alone);
  see "Ring statistics".
- **Minimum ring size to search**: smallest ring (in atoms) the search
  reports; raise to exclude small rings (e.g. triangles) you consider noise.
  Default 3 (the smallest a closed ring can topologically be — 1- and 2-atom
  "rings" cannot occur).
- **Maximum ring size to search**: largest ring (in atoms) the search looks
  for; the main cost knob for ring analysis. Default 8.
- **Visualization**: preserve colors or color particles by hybridization,
  cluster identifier, cluster size, same-phase coordination, or ring size.

## Particle properties

- `Carbon Coordination`: number of neighbors inside the C-C cutoff.
- `Hybridization`: 0 = other, 1 = sp, 2 = sp2, 3 = sp3.
- `Carbon Cluster Identifier`: connected-component ID; 0 means the atom is not
  part of the selected hybridization state.
- `Carbon Cluster Size`: size of the atom's cluster.
- `Carbon Cluster Rank`: 1 is the largest cluster.
- `Relevant Carbon Cluster`: 1 when the cluster reaches the configured minimum
  size, otherwise 0.
- `Same-Phase Coordination`: of an atom's neighbors, how many share its
  hybridization (0 for atoms outside the tracked phase). This is the most
  damage-sensitive per-atom quantity the modifier produces — on
  `camorfo_sp3_60_mCRA` the population with all four neighbors sp3 halves
  between cycle 0 and 100, against 4% for the connected component — and
  **Color by same-phase coordination** maps it in space: dark where an atom is
  deep inside its phase, light at its edge.
- `Ring Size` (only when ring analysis runs): size, in atoms, of the smallest
  primitive ring the atom belongs to; 0 if it belongs to none. See "Ring
  statistics".
- `Smallest Ring Identifier` (Int64, only when ring analysis runs): ID of that
  smallest ring (ties broken by lowest ID), 0 if none. Select a single ring
  with Expression Selection, e.g. `SmallestRingIdentifier==42`, to isolate and
  count its atoms.
- `Ring Membership Count` (only when ring analysis runs): number of distinct
  primitive rings the atom belongs to (an atom shared by two fused rings has
  value 2, and so on).

## Why the two cutoffs match by default

The bond cutoff used to default to 1.85 angstrom, shorter than the
coordination cutoff. That was wrong in two ways, both measured on
`camorfo_sp3_60_mCRA` cycle 0 (473344 atoms):

1. **It discarded real bonds.** 2.0% of all C-C bonds inside the coordination
   cutoff are longer than 1.85 angstrom, and **33585 sp3 atoms (11.3% of the
   phase)** were connected through fewer bonds than had determined their
   hybridization. An atom could be sp3 *because of* a 1.9-angstrom bond and
   still be cut off from the neighbor at the other end of it.
2. **It sat on a slope, not in a gap.** The C-C bond-length distribution is
   still populated and roughly flat around 1.85 (~3400-3600 counts per
   0.025-angstrom bin from 1.86 to 1.96), so small changes in the cutoff moved
   many bonds. Cluster counts varied by a factor of 5.5 across a ±5% change.

At 2.064 angstrom the distribution has already vanished (21 counts in the last
bin, 0 beyond), so the choice is on a plateau: bond cutoffs of 2.00, 2.064 and
2.15 give cluster counts within ±1 of each other and identical
`largest_cluster_fraction` to four decimals.

The modifier now reports `AmorphousCarbon.atoms_with_discarded_bonds` and
raises a pipeline **warning** whenever the bond cutoff is shorter than the
coordination cutoff. In batch mode the tracking modifier downstream replaces
the pipeline status, so `--carbon-track` prints the warning to the console once
per run instead and records the count in the CSV.

A shorter bond cutoff is still a legitimate choice if a deliberately stricter
connectivity criterion is wanted — just not as a default, and not without
reporting the cutoff alongside any cluster count.

**The mirror-image mistake is worse.** A bond cutoff *longer* than the
coordination cutoff (say 2.1 against 1.9) fails twice over: clusters propagate
along bonds that never counted towards any hybridization, and the short
coordination cutoff under-counts neighbors so that real sp3 atoms are demoted
to sp2 — 20,003 of them, 6.7% of the phase, at coord 1.9 on
`camorfo_sp3_60_mCRA`. `AmorphousCarbon.atoms_with_uncounted_bonds` counts the
atoms holding a bond in the gap, and the modifier warns.

That count deliberately runs over **all** atoms rather than just the tracked
phase. The atoms carrying a stretched bond are in locally expanded
environments, so the short cutoff is precisely what pushes them out of the
phase: at coord 1.9 / bond 2.1, *zero* sp3 atoms hold such a bond while 20,336
atoms overall do. A phase-restricted count would report "no problem" exactly
when the problem is worst.

## Color palette

All coloring modes in both modifiers draw from one validated palette. No entry is
light: every one clears 3:1 WCAG contrast against a white viewport background, so
nothing washes out or reads as white.

| role | hex | used for |
|---|---|---|
| sky blue | `#1E9DD9` | sp3 · main fragment |
| amber | `#C58702` | sp2 · dispersed |
| indigo | `#4A3AA7` | sp · minor fragment |
| teal | `#128E7E` | left core (still in phase) |
| deep red | `#B2182B` | left phase |
| graphite | `#4D4D4D` | atoms outside the phase · not tracked |

Worst separation across the **chromatic pairs**: ΔE 11.9 under simulated
deuteranopia, 13.1 under protanopia and 13.8 with normal vision (OKLab ×100),
so the categories stay distinct for red-green colorblind readers — about 8% of
men. In `Cluster Fate`, cool hues mean "still in the tracked phase" and warm
hues "left it", which keeps the main distinction readable even where hue
separation is weakest; teal for *left core* is cool on purpose, because those
atoms are still sp3 and a red atom must mean phase loss and nothing else.
Before teal was added the five-entry palette reached ΔE 18.1 / 21.7 / 23.4 on
the same measure; a sixth category necessarily costs separation, and teal was
the candidate that gave up the least (the alternatives tested landed at 8.6–10.0
under deuteranopia).

Two generated (non-categorical) encodings:

- **Color by cluster identifier** steps hue by the golden ratio in OKLCH at
  constant chroma, alternating lightness between 0.48 and 0.64 for consecutive
  IDs. With thousands of clusters no palette can make every pair identifiable, so
  this only separates *neighbouring* clusters; the lightness alternation is what
  makes that work under dichromatic vision, where the hue circle collapses onto a
  single axis (worst consecutive pair ΔE 8.8 instead of 1.7).
- **Color by cluster size** is a single-hue sequential ramp, light to dark
  (`#60AAF3` → `#00447A`), as magnitude encodings require. The light end stops
  well short of white at 2.5:1 against it.

The palettes were checked with the computable color checks (OKLCH lightness band,
chroma floor, all-pairs protan/deutan separation, normal-vision separation, WCAG
contrast) rather than chosen by eye.

## Clustering a percolating network

Connected-component analysis is **degenerate** for a network above the
percolation threshold, which is where sp3-rich amorphous carbon sits. On
`camorfo_sp3_60_mCRA` the largest sp3 component holds 98.5% of the phase at
cycle 0 and still 94.6% at cycle 100, and a real percolation test says the
network spans the cell at *every one* of the 101 cycles. The cutoffs cannot fix
this: the question "are these atoms connected?" simply has no resolving power
in that regime, because the sp3 subnetwork has a mean internal degree of 2.585,
far above the ~1.5 needed to percolate.

**Minimum same-phase neighbors** restricts clustering to the well-embedded core
of the phase. With the parameter at 3, only sp3 atoms having at least 3 sp3
neighbors are clustered, and the analysis becomes discriminating:

| | whole phase | core (≥ 3 same-phase neighbors) |
|---|---|---|
| clustered atoms, cycle 0 | 298,356 | 166,159 |
| largest / clustered, cycle 0 | 0.9850 | 0.9120 |
| largest / clustered, cycle 100 | 0.9456 | **0.0299** |
| percolates at cycle 100 | yes | **no** |

The core loses percolation across cycles ≈29–37 — not at a single sharp cycle.
Spanning flickers on and off through that band (lost at 29, regained at 30, 31,
32, 34 and 36) and the order parameter fluctuates with it. It is not gone for
good after the band either: cycle **58** spans again, with a largest cluster of
only 6.4% of the clustered core — a filament, not a network. That is the
expected behavior of a finite system *at* the percolation threshold, not
measurement noise: every frame is evaluated independently and
deterministically, and cycle 58 reproduces on re-running. Report the band, never
a single crossing cycle from the boolean flag, and plot
`largest_cluster_core_fraction`, which decays clearly overall though it too
fluctuates from cycle to cycle inside the band.

The collapse survives the coordination-cutoff sweep (it happens at 1.95, 2.064
and 2.15 alike), so it is not a threshold artifact.

Two approaches that measurement ruled out on this sample, before spending effort
on them:

- **k-core decomposition** for k ≥ 3 is empty at both cycle 0 and cycle 100 —
  the sp3 network never has a 3-core, so it cannot serve as an order parameter.
- **Rigidity percolation** cannot discriminate either: mean coordination goes
  3.63 → 3.54 against a Phillips-Thorpe rigidity threshold of 2.4, so the sample
  is deep in the rigid regime at both ends.

## Global attributes

All attributes live under the `AmorphousCarbon.*` namespace.

Composition — `sp_count`, `sp2_count`, `sp3_count`, `other_count` and the
matching `sp_percent`, `sp2_percent`, `sp3_percent`, `other_percent`, which are
percentages of **all** particles.

Clusters — `cluster_count`, `relevant_cluster_count`, `largest_cluster_size`,
`largest_cluster_fraction`, `clustered_fraction`,
`relevant_clustered_fraction`, `second_largest_cluster_size`,
`largest_cluster_radius_of_gyration`, `largest_cluster_percolates` (0/1).
`clustered_fraction` is always the fraction of selected atoms belonging to a
component of at least two atoms; `relevant_clustered_fraction` uses the
configurable minimum cluster size.

Size distribution and the Robertson gap estimate (added 2026-08-01, ported
from the reference Python pipeline; see "Remaining metrics from the Python
pipeline") — `mean_cluster_size`, `median_cluster_size`, `cluster_size_p90`,
`cluster_number_density` (clusters per cubic angstrom, `cluster_count /
simulation_cell_volume`), `gap_estimate_eV`. All four size-distribution
attributes are computed over **every** connected component of the target
phase (matching `cluster_count`'s scope), not filtered by the relevance
threshold.

Core filter — `min_same_phase_neighbors` (echoes the parameter),
`clustered_atom_count` (atoms that passed the filter and were therefore
eligible for clustering), `largest_cluster_core_fraction` (largest cluster over
`clustered_atom_count`, which is the percolation order parameter when the
filter is in use) and `clustered_core_fraction` (its counterpart for
`clustered_fraction`).

Parameters and diagnostics — `coordination_cutoff`, `cluster_bond_cutoff`,
`bond_cutoff` (compatibility alias of the previous one),
`atoms_with_discarded_bonds` and `atoms_with_uncounted_bonds`.

Rings (only populated when ring analysis runs; see "Ring statistics") —
`ring_analysis_enabled` (0/1), `ring_min_size_searched`, `ring_max_size_searched`,
`ring_count_total`, `ring_mean_size`, and `ring_count_3` .. `ring_count_<max size>`
(one attribute per ring size found, including sizes below the minimum, which
are always 0).

**Careful with the fraction denominators under a core filter.**
`largest_cluster_fraction` and `clustered_fraction` divide by the **whole
phase**, not by the atoms the filter admitted, so with the filter on they fall
for two reasons at once and one of them is not fragmentation. On
`camorfo_sp3_60_mCRA` cycle 0 with the filter at 3:

| | value | denominator |
|---|---|---|
| `clustered_fraction` | 0.5443 | all 298,356 sp3 atoms |
| `clustered_core_fraction` | **0.9773** | the 166,159 eligible ones |
| `largest_cluster_fraction` | 0.5079 | all sp3 atoms |
| `largest_cluster_core_fraction` | **0.9120** | the eligible ones |

Reading 0.5443 as "45% of the sp3 phase is not clustered" is wrong: 44% of the
phase was never eligible, and of what was, 97.7% sits in a component of at
least two atoms. Use the `*_core_*` pair whenever the filter is on, and never
compare a whole-phase fraction between runs with different filter settings. The
older attributes keep their original definition rather than silently changing
meaning when a parameter changes; the pipeline status spells the split out when
the filter is active.

The correct denominator also makes a result visible that the old one hid.
Across the full series:

| cycle | `clustered_core_fraction` | `largest_cluster_core_fraction` |
|---|---|---|
| 0 | 0.9773 | 0.9120 |
| 29 | 0.9517 | 0.2167 |
| 100 | 0.9390 | 0.0299 |

The core barely atomizes — 94% of eligible atoms are still in a component of at
least two at cycle 100 — while the largest component collapses by a factor of
30. The core therefore **breaks into many mid-sized pieces rather than dusting
into isolated atoms**, which is a different failure mode from the one
`clustered_fraction` suggested when it slid from 0.54 to 0.40.

## Locating the percolation transition

`second_largest_cluster_size` is the observable to plot. In percolation the
runner-up cluster peaks at the transition — it is largest exactly when the
spanning cluster is coming apart — so it locates the transition without
depending on the spanning test.

**Do not quote it for a single cycle.** Measured on `camorfo_sp3_60_mCRA` at
every cycle rather than the seven the earlier prototype sampled, it rises
smoothly from 97 (cycle 0) to ~2,500 by cycle 28, and then oscillates between
1,361 and 12,921 for the rest of the series — adjacent cycles differ by up to
8x. Its maximum over the whole series falls at cycle 29, but the prototype's
sparse sampling made a clean peak at cycle 35 appear where there is none. Past
the transition the identity of the largest cluster itself jumps between
comparable fragments, so the runner-up jumps with it. Plot the band and a
smoothed curve; the pre-transition rise is the clean part.

**Spanning recurs later than the flicker band suggests.** The core stops
spanning at cycle 29 and spanning then flickers (regained at 30, 31, 32, 34,
36), but it also recurs once at cycle **58**, with a largest cluster of 7,508
atoms — 6.4% of the clustered core. A cluster that small spanning the cell is a
filament, not a percolating network, which is the sharpest argument for not
using the boolean flag as the order parameter at all.

`largest_cluster_radius_of_gyration` confirms the same transition by another
route, **but is not a cluster size while the cluster percolates**. A spanning
cluster touches its own periodic image, so no unwrapping of it is well-defined
(that is precisely what the percolation test detects) and the value saturates
at the scale of the cell — ~75 Å in a ~160 Å cell here, i.e. "fills the box".
Read it only on rows where `largest_cluster_percolates` is 0. The value is
reproducible in both regimes, because the traversal is deterministic; it is
just not a size in the first one.

## Ring statistics

**Detect closed rings (SP/primitive)** finds closed carbon rings in the
same-phase bond graph and reports how many atoms each is made of — this is
what isolates the closed rings visible when clustering sp3 (or sp2) atoms
with **Cluster bond cutoff**/**Coordination cutoff** set so that a ring reads
as a clean loop (e.g. `camorfo_sp3_60_mCRA` cycle 100, sp3). It is off by
default because it costs meaningfully more than clustering alone; enable it
or pick **Color by ring size** to run it.

**Ring definition.** A closed walk is not a useful count on its own: a dense,
percolating sp3 network has thousands of closed walks per atom once you allow
combinations of smaller rings. The modifier instead uses the standard
**shortest-path (SP) / primitive ring** criterion of D. S. Franzblau, *Phys.
Rev. B* **44**, 4925 (1991) — the same definition used for ring statistics in
amorphous carbon and silica in the literature: for every bond `(i, j)`, find
the shortest alternative path connecting `i` and `j` that does not use that
bond directly; the resulting cycle counts as a ring only if no pair of atoms
on it is connected by a *shorter* path through the rest of the graph. That
second test is what rejects a ring that is really the sum of two smaller
rings sharing an edge (e.g. two fused hexagons would otherwise also look like
one ten-membered ring through their outer bonds). Ties among several
equally-short alternative paths through the same bond are all enumerated and
checked independently, matching Franzblau's original prescription, and are
capped (32 candidate paths, 64 tied predecessors per node) to stay tractable
in locally symmetric diamond-like regions — a soft limit, not expected to bind
on a genuinely amorphous (non-crystalline) network.

**Rings must close in real space.** The search tracks the integer periodic-
image shift along every candidate path (the same bookkeeping the cluster
percolation test uses) and only accepts a cycle whose net shift around the
loop is exactly zero. A path that only "closes" by wrapping around the
periodic cell is a percolation loop, not a molecule, and is never reported as
a ring. In practice this is a non-issue for any realistic amorphous-carbon
cell: a ring of `maxRingSize` atoms spans at most a few bond lengths (~15-20
Å for the default maximum of 8), far shorter than a simulation cell of
hundreds of Å, so no real ring is ever close to wrapping.

**Maximum ring size to search** bounds the shortest-path search depth and is
the main cost knob: raising it mostly spends time on bonds that never close
into a small ring. Real rings in amorphous carbon are almost always well
under 10 atoms; the default is 8.

**Minimum ring size to search** filters the *reported* rings after detection:
a bond whose shortest primitive ring is smaller than the floor is simply not
counted (there is no well-defined "next larger" ring through the same bond in
this criterion, so raising the floor removes rings rather than searching
further for bigger ones through the same bond). The floor cannot go below 3,
the smallest ring a simple graph can contain at all — a 1- or 2-atom "ring" is
not a graph-theoretic possibility and this modifier never produces one, with
or without the floor. On `camorfo_sp3_60_mCRA` cycle 100 sp3 (default
cutoffs, `maxRingSize` = 8) raising the floor from 3 to 4 removes only 81 of
32,485 rings — real 3-membered rings (triangles) are rare and highly strained
in a tetrahedral network — while raising it to 6 removes 13,688 more (down to
18,797), since it now excludes 4- and 5-membered rings too; the mean size
rises monotonically in both cases (5.87 → 5.88 → 6.54), exactly as expected
when the smaller end of the distribution is cut off.

**Rings are not the same as distinguishable cluster shape.** Before this
per-bond ring search existed, the only way to spot a "ring" was by eye, from
small connected-component *clusters* that happened to look loop-shaped —
which depends on how fragmented the network is, not on whether real closed
cycles exist. On `camorfo_sp3_60_mCRA` cycle 0 with a mismatched
coordination/bond cutoff (1.9/2.3 — see the warning below), 99.6% of the sp3
phase sits in one single giant cluster, so there is no small-cluster
substructure left to look at; at cycle 100 the same network has fragmented
into 1,773 clusters, many small enough to visually read as rings. That made
cycle 0 look ring-free and cycle 100 look ring-rich by eye. The primitive-ring
count says the opposite: measured with the same 1.9/2.3 cutoffs, cycle 0 has
**57,687** primitive rings (mean size 5.33) against cycle 100's 38,285 (mean
4.78) — cycle 0 is *less* damaged and *more* diamond-like, so it genuinely
contains more real closed rings; there simply weren't small enough clusters
left over to see them by eye. Trust `Ring Size` / `ring_count_total` over
visual cluster shape for anything you plan to report as a ring count.

**Match the two cutoffs for ring analysis too.** The 1.9/2.3 example above
also triggers the bond-cutoff-longer-than-coordination-cutoff warning from
"Why the two cutoffs match by default" — and here it is severe: 209,000-
273,000 atoms (roughly half the sp3 population) hold a bond in the graph the
ring search walks that never counted towards their hybridization. Both the
cluster and the ring numbers computed under that mismatch are still
internally consistent and reproducible, but they describe a graph that is not
the one that defined sp3 in the first place. Set **Cluster bond cutoff** equal
to **Coordination cutoff** (both default to 2.064) unless deliberately
studying a stricter or looser connectivity criterion.

**Validated against known ring statistics.** On `camorfo_sp3_60_mCRA` cycle 0
(the least damaged, most diamond-like configuration), the sp3 primitive rings
average **6.004** atoms — matching diamond's ideal all-hexagonal
(chair-conformation) ring structure to three significant figures, which is
exactly what an intact tetrahedral network should produce. Under damage
(cycle 100) the mean falls to 5.87 as defects introduce more 5-membered
rings, and the count of rings falls from 60,915 to 32,485 alongside the drop
in sp3 population (298,356 → 264,436 atoms) — both physically expected
trends. The fragmented sp2 phase at cycle 100 (largest cluster only 22 atoms,
far from an intact graphitic sheet) correspondingly yields far fewer rings
(63, mean 5.22) than the percolating sp3 matrix. Measured with `maxRingSize`
= 8; runtime cost on the full 473,344-atom configuration was about 10 seconds
on top of the existing cluster analysis.

**Isolating one ring.** Use Expression Selection with
`SmallestRingIdentifier==<id>` to select exactly the atoms of one ring (read
an ID off the viewport or a picked atom's attributes), then read the
selection count for its size — which will match `Ring Size` for every atom in
it, since ties are broken deterministically. **Color by ring size** gives a
faster overview: light for small rings, dark for large ones (a warm hue, kept
visually distinct from the cool cluster-size ramp), with atoms outside any
found ring in graphite.

## Remaining metrics from the Python pipeline

`AmorphousCarbon.mean_cluster_size`, `median_cluster_size`, `cluster_size_p90`,
`cluster_number_density` and `gap_estimate_eV` (added 2026-08-01) are the last
of the metrics the reference Python pipeline
(`ac_analyzer.analysis.sp2_clusters.sp2_cluster_metrics`) computes that had
not been ported to the native modifier. Percolation, radius of gyration and
the second-largest cluster were already native (see "Clustering a percolating
network" and "Locating the percolation transition"); the ordered
cluster-size distribution and the heuristic Robertson gap estimate were the
remaining gap.

The four size-distribution attributes are computed over the same population
as `cluster_count` and `second_largest_cluster_size` — every connected
component of the target phase, **not** filtered by the "Minimum relevant
cluster size" threshold, matching the Python reference's scope exactly (that
threshold only ever affected `relevant_cluster_count` /
`relevant_clustered_fraction`, never the plain `cluster_count`-scoped
attributes). `median_cluster_size` and `cluster_size_p90` reproduce NumPy's
default conventions (`np.median`'s two-middle-element average on an
even-length array; `np.percentile`'s linear interpolation between the two
closest ranks) so that a script written against the Python pipeline's output
can switch to the C++ CSV without a different percentile definition.
`cluster_number_density` divides `cluster_count` by the simulation cell's true
3D volume (the determinant of the cell matrix, so it is exact for triclinic
cells too, unlike the Python reference's `Lx*Ly*Lz` product which assumes an
orthogonal box — the two agree exactly on `camorfo_sp3_60_mCRA`, which is
orthogonal).

**The Robertson gap estimate is a heuristic upper bound, not a DFT-level
calculation.** Robertson & O'Reilly, *Phys. Rev. B* **35**, 2946 (1987) show
that for compact graphitic clusters of fused six-membered rings the
electronic gap scales as `E_g ≈ 2|β|/√M`, with `M` the number of rings and
`|β| ≈ 2.9 eV` the graphite π resonance integral. `gap_estimate_eV` converts
the **largest** cluster's size to a ring count with `M ≈ N_atoms / 2` (a
compact graphene-like sheet has about two sp2 atoms per ring) and evaluates
that relation; it returns 0 below the smallest cluster that can define a ring
(`M < 1`). The relation assumes compact sheets, not chains or isolated rings,
so it should be read as "how graphitic would a sheet this size look", not as
a predicted optical gap — the same caveat the Python reference documents.

**Validated exactly against the Python reference** on `camorfo_sp3_60_mCRA`
cycles 0, 50 and 100, both phases (30 checks total,
`research/amorphous_carbon/scripts/crosscheck_size_stats.py`): all five attributes
agree with `sp2_cluster_metrics`'s `mean_size` / `median_size` / `size_p90` /
`cluster_number_density` / `gap_estimate_eV` to floating-point precision
(differences at the 1e-16-1e-19 level, i.e. rounding noise, not disagreement).

## Current scope

This first stage analyzes each frame independently and supports OVITO's full
simulation-cell geometry and periodic boundary conditions. Cancellation is now
checked periodically inside the connected-components search itself (not just
between clusters), so a single very large percolating cluster (e.g. the sp3
matrix) no longer makes the interface appear to freeze until the whole BFS
finishes.

Cross-frame correspondence (cycle 0 vs. cycle N) is implemented by a second,
downstream modifier — see **Amorphous carbon cluster tracking** below. To run
both over a whole cycle series without the GUI, see **Batch mode** at the end
of this document.

# Amorphous carbon cluster tracking

Native OVITO modifier that matches the clusters found by **Amorphous carbon
cluster analysis** between a reference configuration and the current one, so
you can see how the largest sp2 or sp3 domain breaks apart over an mCRA run.
It appears in the **Analysis** modifier category as **Amorphous carbon
cluster tracking**, and is built on the same reference/current infrastructure
as OVITO's Displacement Vectors and Wigner-Seitz modifiers.

## Pipeline requirement

This modifier does not detect hybridization or connectivity itself. Place an
**Amorphous carbon cluster analysis** modifier upstream of it — the tracking
modifier reads its `Carbon Cluster Identifier` / `Carbon Cluster Size` /
`Carbon Cluster Rank` output, both in the current pipeline output and in the
reference configuration (which is evaluated through the *same* upstream
pipeline, so it must contain the cluster-analysis modifier too). By default
the reference configuration is "upstream pipeline, frame 0", so with no
extra configuration this compares the current animation frame against frame
0 (typically cycle 0).

Atom correspondence between the two frames uses `Particle Identifier`, so the
input file format must provide stable per-atom IDs (LAMMPS data/dump files
with an `id` column do).

## Parameters

- **Visualization**: preserve colors, color particles by cluster fate, or
  color by cluster genealogy (see "Bidirectional cluster genealogy" below).
- **Genealogy overlap threshold** (`genealogyOverlapFraction`, default 0.10):
  minimum fraction of either cluster's own size that a shared-atom overlap
  must reach before it counts as an edge in the bidirectional cluster
  classification. See "Bidirectional cluster genealogy".

## Particle properties

- `Reference Cluster Identifier` (Int64): the **rank** (1 = largest) of the
  reference-configuration cluster this atom belonged to, if it was part of a
  tracked-phase cluster there and its ID could be matched to the reference;
  otherwise 0. Because this is a rank, not a raw internal ID, atoms with
  value `1` always come from whichever cluster was scientifically largest at
  the reference time.
- `Cluster Fate` (Int32): per-atom outcome relative to its reference cluster.
  - `0` = Not tracked (wasn't in a tracked-phase cluster at the reference, or
    its particle ID could not be matched between the two configurations).
  - `1` = Left phase (no longer in the tracked hybridization state).
  - `2` = Main fragment (still clustered, and in the current-frame cluster
    that inherited the largest share of its reference cluster's atoms — a
    cluster that survives 100% intact shows entirely as this value, with a
    single fragment).
  - `3` = Minor fragment (still clustered, but in a smaller offshoot).
  - `4` = Dispersed (still in the tracked phase, but now an isolated atom).
  - `5` = Left core (still in the tracked hybridization state, but the upstream
    **minimum same-phase neighbors** filter no longer clusters it). Only ever
    produced when that filter is on; see "Left phase is not left core" below.
- `Reference Cluster Genealogy` (Int32, see "Bidirectional cluster genealogy"):
  a property of the atom's **reference** cluster, broadcast to every current
  atom that traces back to it. `0` = Not tracked (no relevant reference
  cluster), `1` = Disappeared, `2` = Stable, `3` = Fragmented.
- `Cluster Genealogy` (Int32, see "Bidirectional cluster genealogy"): a
  property of the atom's own **current** cluster. `0` = Not clustered (not in
  a relevant current cluster), `1` = Created, `2` = Stable, `3` = Merged.

## Global attributes

Reported under the `AmorphousCarbonTracking.*` namespace, computed for the
single largest reference cluster (rank 1) only — that is the cluster whose
breakup is the relevant radiation-damage signal:

```text
AmorphousCarbonTracking.reference_largest_cluster_size
AmorphousCarbonTracking.main_fragment_size
AmorphousCarbonTracking.main_fragment_fraction
AmorphousCarbonTracking.retained_fraction
AmorphousCarbonTracking.left_phase_fraction
AmorphousCarbonTracking.left_core_fraction
AmorphousCarbonTracking.dispersed_fraction
AmorphousCarbonTracking.vanished_fraction
AmorphousCarbonTracking.fragment_count
AmorphousCarbonTracking.unmapped_atom_count
AmorphousCarbonTracking.vanished_reference_atom_count
```

The same fate breakdown is reported a second time, pooled over **every**
reference cluster at or above the upstream "Minimum relevant cluster size"
(see "Pooled aggregates over all relevant clusters" below):

```text
AmorphousCarbonTracking.relevant_reference_atom_count
AmorphousCarbonTracking.relevant_main_fragment_size
AmorphousCarbonTracking.relevant_main_fragment_fraction
AmorphousCarbonTracking.relevant_retained_fraction
AmorphousCarbonTracking.relevant_left_phase_fraction
AmorphousCarbonTracking.relevant_left_core_fraction
AmorphousCarbonTracking.relevant_dispersed_fraction
AmorphousCarbonTracking.relevant_vanished_fraction
AmorphousCarbonTracking.relevant_fragment_count
AmorphousCarbonTracking.relevant_mean_fragments_per_cluster
```

The bidirectional genealogy adds a third group, computed over **every**
relevant cluster on each side rather than just rank 1 (see "Bidirectional
cluster genealogy" below):

```text
AmorphousCarbonTracking.genealogy_overlap_fraction
AmorphousCarbonTracking.reference_clusters_relevant
AmorphousCarbonTracking.reference_clusters_stable
AmorphousCarbonTracking.reference_clusters_fragmented
AmorphousCarbonTracking.reference_clusters_disappeared
AmorphousCarbonTracking.reference_clusters_stable_fraction
AmorphousCarbonTracking.reference_clusters_fragmented_fraction
AmorphousCarbonTracking.reference_clusters_disappeared_fraction
AmorphousCarbonTracking.current_clusters_relevant
AmorphousCarbonTracking.current_clusters_stable
AmorphousCarbonTracking.current_clusters_merged
AmorphousCarbonTracking.current_clusters_created
AmorphousCarbonTracking.current_clusters_stable_fraction
AmorphousCarbonTracking.current_clusters_merged_fraction
AmorphousCarbonTracking.current_clusters_created_fraction
```

The `*_fraction` attributes are omitted from the `--carbon-track` CSV (they
are trivially derivable from the counts, e.g. `reference_clusters_stable /
reference_clusters_relevant`) to keep the fixed CSV schema from growing by
another six columns; they are available in the GUI and to any script reading
attributes directly.

The two mapping counters describe opposite failures. `unmapped_atom_count` is
current-frame atoms with no counterpart in the reference (new atoms);
`vanished_reference_atom_count` is reference atoms with no counterpart now
(deleted atoms). Both are 0 for an mCRA series, where the atom set is fixed.

`fragment_count` only counts distinct current-frame clusters of size >= 2
(singletons go into `dispersed_fraction` instead), matching the existing
"clustered" convention used by `AmorphousCarbon.clustered_fraction`.

`retained_fraction + left_phase_fraction + left_core_fraction +
vanished_fraction` = 1 exactly: every atom of the reference cluster falls in
exactly one of the four. `vanished_fraction` is the share that no longer exists
in the current configuration — without it the other three silently failed to
account for those atoms, so the balance did not close on any trajectory that
deletes atoms.

## Left phase is not left core

An atom carries no cluster identifier for two entirely different reasons: it
left the tracked hybridization state, or it is still in that state and the
**minimum same-phase neighbors** filter dropped it from the clustered core.
`left_phase_fraction` counts only the first; `left_core_fraction` counts the
second. Reporting their sum as phase loss overstates the damage badly — on
`camorfo_sp3_60_mCRA` with the filter at 3, cycle 10:

| fraction of the reference cluster | cycle 10 | cycle 100 |
|---|---|---|
| `retained_fraction` | 0.6605 | 0.2518 |
| `left_phase_fraction` (actually left sp3) | 0.1596 | 0.4260 |
| `left_core_fraction` (still sp3, below threshold) | 0.1799 | 0.3221 |
| the two summed, i.e. what was reported before | 0.3395 | 0.7482 |

Roughly half of what a single combined number calls "left sp3" is still sp3.
That the split is the right one is confirmed independently: genuine phase loss
measured from the **core** cluster at cycle 100 is 0.4260, against 0.4227
measured from the **whole-phase** cluster in the run without the filter. Those
are different atom populations (151,543 vs. 293,880) and they agree to 0.3 pp,
so the rate of real sp3 loss does not depend on which one you start from.

With the filter off, `left_core_fraction` is identically 0 and every other
metric is bit-for-bit what it was before the split, so results computed without
the core filter are unaffected.

## Pooled aggregates over all relevant clusters

`retained_fraction` and its siblings describe the rank-1 (largest) reference
cluster only. For a percolating sp3 matrix that is the whole story — rank 1
holds 293,880 of the 294,703 atoms in relevant reference clusters on
`camorfo_sp3_60_mCRA`, so it *is* the phase. For a finely divided phase it is
nearly meaningless: in **sp2** mode on the same sample the largest cluster holds
**14 atoms out of 174,096**, and `retained_fraction = 0.500` is a coin flip on
14 atoms, not a measurement.

The `relevant_*` attributes recompute exactly the same breakdown pooled over
every reference cluster at or above the upstream "Minimum relevant cluster size"
(default 3). Nothing else changes: an atom is classified retained / left-phase /
left-core / dispersed by the same rules, the same per-cluster majority vote picks
each cluster's main fragment, and the denominator is the number of reference
atoms in those clusters, so `relevant_retained + relevant_left_phase +
relevant_left_core + relevant_vanished = 1` exactly, as for rank 1.

`relevant_fragment_count` is the total number of distinct current-frame clusters
the relevant reference clusters scattered into, summed over clusters; a
reference cluster with no surviving clustered atom contributes 0.
`relevant_mean_fragments_per_cluster` divides that by the number of relevant
reference clusters (**not** by the number that produced at least one fragment,
so it is comparable across frames).

No new parameter: the population is set by the relevance threshold the upstream
modifier already applies. A separate "top N clusters" knob was considered and
rejected — it would not fix the sp2 case at all, since the ten largest sp2
clusters together are still ~100 atoms.

Measured (cutoffs 2.064/2.064, min relevant size 3, cycle 0 as reference):

```text
run           rank-1 size   pooled atoms   rank-1 retained   pooled retained
sp3 c0->c10       293880         294703          0.85791          0.85796
sp3 c0->c100      293880         294703          0.57728          0.57722
sp2 c0->c100          14          35090          0.50000          0.45534
```

The sp3 rows are the correctness check: with a dominant cluster the pooled value
must reproduce the rank-1 value, and it does to 5 decimal places. The sp2 row is
the reason the attributes exist — 35,090 atoms of statistics instead of 14, and a
retained fraction 4.5 pp away from the rank-1 number.

The pipeline status message also states the pooled figures, and adds an explicit
warning whenever rank 1 holds under 5% of the relevant reference atoms:

```text
The rank-1 cluster holds only 0.04% of the relevant reference atoms: quote
the pooled figures, not the rank-1 ones, for this phase.
```

**Validation.** `research/amorphous_carbon/scripts/crosscheck_pooled.py` reimplements the
pooled metrics from the description above (independently, on top of the same
`cKDTree` + `connected_components` cluster reconstruction the other cross-check
scripts use) and compares all ten against the native `--carbon-track` CSV on the
three cases in the table: 30/30 exact, integers bit-for-bit and fractions within
1e-12. The synthetic C++ self-test covers them too, including the sum-to-one
invariant (see "C++ self-test").

## Bidirectional cluster genealogy

The rank-1-only fate/fraction attributes above answer "what happened to the
single largest reference cluster". They cannot show a current-frame cluster
built mostly from atoms that were *not* part of any tracked-phase cluster in
the reference — there is no reference cluster whose fate that atom could be
counted under. **Reference Cluster Genealogy** / **Cluster Genealogy** close
that gap by classifying **every** relevant cluster (upstream "Minimum relevant
cluster size") on both sides, independently of rank.

**Algorithm.** Build the atom-overlap bipartite graph between relevant
reference clusters and relevant current clusters, using the same
`Particle Identifier` matching as the rest of the modifier. An edge `(R, C)`
exists when the atoms `R` and `C` share reach **Genealogy overlap threshold**
(`genealogyOverlapFraction`, default 0.10) of *either* cluster's own size:

```text
overlap(R, C) >= threshold * size(R)   OR   overlap(R, C) >= threshold * size(C)
```

The OR is deliberate: it also catches a reference cluster shedding a small but
*pure* offshoot (a handful of atoms that all end up in one small new cluster,
even though they are a negligible fraction of the much larger reference
cluster they came from) and, symmetrically, a small reference cluster fully
absorbed into a much larger current cluster.

Each cluster is then labeled from its **own** edge count in that graph — the
standard overlap-tracking scheme used for storm-cell and grain tracking (e.g.
Dixon & Wiener 1993, "TITAN"):

```text
reference cluster R: 0 edges -> Disappeared, 1 edge -> Stable, >=2 edges -> Fragmented
current cluster   C: 0 edges -> Created,     1 edge -> Stable, >=2 edges -> Merged
```

Because each side's label depends only on its own edges, a reference cluster
can be `Stable` (its identity continues into exactly one successor) while that
successor is itself `Merged` (it also absorbed atoms from another reference
cluster) — that asymmetry is the point of a *bidirectional* classification,
and exactly the case the plan's original taxonomy was meant to capture.

**Validated on `camorfo_sp3_60_mCRA`** against an independent Python
implementation (`research/amorphous_carbon/scripts/crosscheck_genealogy.py`), bit for
bit at the default threshold:

| case | reference: stable / fragmented / disappeared | current: stable / merged / created |
|---|---|---|
| sp3, cycle 0 → 10 | 199 / 2 / 13 | 450 / 4 / 12 |
| sp3, cycle 0 → 100 | 194 / 5 / 15 | 1107 / 8 / 34 |
| sp2, cycle 0 → 100 | 3370 / 573 / 6200 | 3723 / 406 / 14015 |

The rank-1 sp3 cluster at cycle 100 is a good illustration: it is
`Fragmented` on the reference side (54.6% of its atoms land in the main
fragment, but several small pure offshoots also clear the threshold) and its
main fragment is `Merged` on the current side (besides inheriting most of its
mass from the rank-1 reference cluster, it also fully absorbed a handful of
small reference clusters).

**Threshold robustness.** Swept 0.05 → 0.30 (a 6x range) on all three cases
above: results are essentially unchanged (e.g. sp3 cycle 0→100 stays at
194/5/15 and 1107/8/34 for both 0.05 and 0.10, moving to only 194/4/16 and
1105/8/36 at 0.20). The default of 0.10 sits in the middle of that plateau.

**Interpreting the sp2 numbers.** With 10143 relevant reference clusters and
most of the sp2 phase in dimers/trimers (see "Current scope and limitations"
below on why rank-1 aggregates are misleading for sp2), the majority — 6200 —
are `Disappeared` and 14015 of 18144 relevant current clusters are `Created`.
This is not a defect: it reflects that individual small sp2 domains have short
lifetimes and mostly reform from atoms that were not themselves part of a
relevant sp2 cluster at the reference, which is exactly the picture a
per-cluster genealogy is supposed to surface for a phase where the rank-1
cluster (14 atoms) is statistically irrelevant.

## Current scope and limitations

- Reports full per-atom detail (`Reference Cluster Identifier` / `Cluster
  Fate`) for **every** tracked-phase reference cluster, not just the largest,
  so smaller clusters can still be inspected with Expression Selection. The
  aggregate `AmorphousCarbonTracking.*` fate fractions come in two forms: the
  original rank-1-only ones, and the `relevant_*` ones pooled over every
  relevant reference cluster (added 2026-08-02; see "Pooled aggregates over all
  relevant clusters"). Use the pooled form for sp2, where rank 1 is 14 atoms.
  The bidirectional genealogy covers every relevant cluster on both sides too,
  per cluster rather than per atom.
- **C++ test harness (added 2026-08-01).** `AmorphousCarbonSelfTestService`
  (`--carbon-track-selftest`, also wired into `ctest` as `AmorphousCarbonSelfTest`
  — see "C++ self-test" below) covers `AmorphousCarbonTrackingModifier`'s
  fate/genealogy logic. The tie-break case from the original hand-built
  synthetic scenario (checked in an unversioned standalone C++ program before
  this harness existed) is not reproduced here — this harness instead covers
  a clear 6-vs-4 majority-vote split plus every other enum value and
  aggregate, and a tie-break would only exercise one extra `if` branch.
  `AmorphousCarbonModifier`'s own clustering has no C++ harness yet, but
  carries lower risk: it is already cross-checked against a real 473k-atom
  Python reference (see "Validation status"). What *is* versioned, under
  `research/amorphous_carbon/`: `crosscheck_carbon.py` (composition and cluster
  counts against the reference Python pipeline), `crosscheck_rg.py` (radius of
  gyration and second-largest cluster against an independent BFS),
  `crosscheck_genealogy.py` (bidirectional cluster genealogy against an
  independent Python implementation), `crosscheck_size_stats.py` (size
  distribution and gap estimate against `sp2_cluster_metrics`),
  `crosscheck_pooled.py` (the pooled `relevant_*` fate aggregates),
  `make_deletion_case.py` (builds the
  missing-atom case that no real frame exercises) and `palette_check.py` (the
  color separation checks).

# Batch mode: `--carbon-track`

Runs the two modifiers above over a whole cycle series without opening the
GUI, and writes one CSV row per frame. This is the only scripted entry point,
since the fork has no Python module.

```bash
./build/bin/ovito --nogui --carbon-track \
  --carbon-track-input '<dir>/cycle*.data' \
  --carbon-track-output out.csv \
  --carbon-track-species sp3
```

Options: `--carbon-track-coordination-cutoff` (default 2.064),
`--carbon-track-bond-cutoff` (2.064), `--carbon-track-species` (sp2|sp3),
`--carbon-track-min-cluster-size` (3),
`--carbon-track-min-same-phase-neighbors` (0 = cluster the whole phase; this is
the flag that enables the core analysis),
`--carbon-track-genealogy-threshold` (0.10; see "Bidirectional cluster
genealogy"), `--carbon-track-rings` (off by
default; see "Ring statistics"), `--carbon-track-max-ring-size` (8) and
`--carbon-track-min-ring-size` (3, both only used with `--carbon-track-rings`),
`--carbon-track-reference-frame` (0), `--carbon-track-first-frame` (0),
`--carbon-track-max-frames` (-1 = all), `--carbon-track-no-sequence-detection`.

The CSV has 67 columns: the `AmorphousCarbon.*` per-frame metrics (including
the five ring summary columns `ring_analysis_enabled`, `ring_min_size_searched`,
`ring_max_size_searched`, `ring_count_total`, `ring_mean_size` — the
per-ring-size breakdown `ring_count_<N>` attributes are only available through
the GUI, since the CSV schema is fixed per run — and the size-distribution
columns `mean_cluster_size`, `median_cluster_size`, `cluster_size_p90`,
`cluster_number_density`, `gap_estimate_eV`), the rank-1-only
`AmorphousCarbonTracking.*` metrics relative to the reference frame, their ten
`relevant_*` counterparts pooled over all relevant reference clusters (see
"Pooled aggregates over all relevant clusters"), the
bidirectional genealogy counts (`genealogy_overlap_fraction`,
`reference_clusters_relevant/stable/fragmented/disappeared`,
`current_clusters_relevant/stable/merged/created` — the `*_fraction` variants
of these are GUI/attribute-only, see "Global attributes"), and the pipeline
status. Rows are flushed as they are
computed, so an interrupted run leaves the frames already computed on disk.

Numbered file sequences are detected and ordered numerically, so
`source_frame N` corresponds to `cycleN.data`.

## Validation status

Verified on 2026-07-30 against `camorfo_sp3_60_mCRA` (101 cycles x 473344
atoms): 101/101 frames `success`, `unmapped_atom_count = 0` throughout,
12m19s wall time, 1.2 GB RSS.

`AmorphousCarbonModifier` output was cross-checked against the reference
Python pipeline (`ac_analyzer.analysis.sp2_clusters`) at the same cutoffs and
agrees **exactly** on `sp2_count`/`sp3_count`, `cluster_count`,
`largest_cluster_size` and `clustered_fraction`, for both the dispersed sp2
phase and the percolating sp3 matrix (cycles 0, 50, 100). The comparison
script is `research/amorphous_carbon/scripts/crosscheck_carbon.py`. The five
size-distribution/gap attributes added 2026-08-01 (see "Remaining metrics
from the Python pipeline") are cross-checked separately, same cycles and both
phases (30 checks), by `research/amorphous_carbon/scripts/crosscheck_size_stats.py`.

`AmorphousCarbonTrackingModifier`'s rank-1 fate/fraction logic has no Python
equivalent, so it rests on `AmorphousCarbonSelfTestService` (see "C++
self-test" below) plus the internal consistency of the full sweep. Its
bidirectional cluster genealogy has both: `research/amorphous_carbon/
crosscheck_genealogy.py` is an independent Python implementation, and matches
the native `--carbon-track` output bit for bit at the default threshold on
three cases (sp3 cycle 0→10, sp3 cycle 0→100, sp2 cycle 0→100) — see
"Bidirectional cluster genealogy" — and the same logic is covered by the
self-test's synthetic scenario.

Ring analysis has no independent reference implementation either, so it rests
on a physical sanity check rather than a crosscheck script: on
`camorfo_sp3_60_mCRA` cycle 0 the sp3 primitive rings average 6.004 atoms,
matching diamond's ideal all-hexagonal ring structure to three significant
figures (see "Ring statistics"), and the trends across damage (cycle 0 vs.
100) and phase (sp3 vs. the fragmented sp2 at cycle 100) all point in the
physically expected direction. Verified single-frame with
`--carbon-track-rings --carbon-track-max-ring-size 8`: ~10s added to the
existing ~10s single-frame cluster analysis on the full 473,344-atom
configuration. The `minRingSize` floor was checked separately for monotonicity
on the same configuration: raising it from 3 to 4 to 6 took the count from
32,485 to 32,404 to 18,797 and the mean size from 5.87 to 5.88 to 6.54, both
moving in the only direction a size floor can move them.

## C++ self-test

`AmorphousCarbonSelfTestService` is an in-process regression test for
`AmorphousCarbonTrackingModifier` -- the first C++ test in this fork (see
"Current scope and limitations" above for why it was missing). It drives the
*actual* modifier class through OVITO's own pipeline machinery (a
`StaticSource` pipeline node holding a hand-built `Particles` object in each
of the reference and current roles -- no file I/O, no reimplemented logic)
against a single synthetic scenario where every expected outcome is known in
advance, because the "Carbon Cluster Identifier" / "Carbon Cluster Size" /
"Carbon Cluster Rank" / "Hybridization" / "Relevant Carbon Cluster" inputs are
set by hand instead of computed by `AmorphousCarbonModifier`.

Run it directly:

```bash
./build/bin/ovito --nogui --carbon-track-selftest
```

or through `ctest` (registered in `src/main/CMakeLists.txt` as
`AmorphousCarbonSelfTest` -- the root `CMakeLists.txt`'s `ENABLE_TESTING()`
had never had an `add_test()` behind it before):

```bash
cd build && ctest -R AmorphousCarbonSelfTest --output-on-failure
```

Both exit non-zero if any check fails. As of 2026-08-02 all 56 checks pass.

**The scenario.** Eight atom groups (33 reference atoms, 34 current atoms,
IDs 1-36), each engineered to land on one specific, known classification:

| group | atoms | designed outcome |
|---|---|---|
| A | 5 | intact cluster, unchanged -> `Cluster Fate` MainFragment, both genealogies Stable |
| B | 12 | the rank-1 (largest) reference cluster: 6 to a main fragment, 4 to a minor fragment (later joined by C), 1 dispersed, 1 left phase -> reference genealogy Fragmented |
| C | 3 | merges entirely into B's minor fragment -> reference genealogy Stable feeding a current cluster that is Merged |
| D | 5 | untracked in the reference, newly clustered now -> current genealogy Created |
| E | 4 | leaves the tracked phase entirely (`Cluster Fate` LeftPhase) -> reference genealogy Disappeared |
| F | 2 | stays in the tracked phase but drops out of the clustered core (`Cluster Fate` LeftCore) -> reference genealogy Disappeared |
| G | 2 | present in the reference only (vanished) -> reference genealogy Disappeared, counts toward `vanished_reference_atom_count` |
| H | 3 | present in the current configuration only (unmapped) -> current genealogy Created, counts toward `unmapped_atom_count` |

This single scenario exercises every value of `Cluster Fate` (all six),
`Reference Cluster Genealogy` (all four) and `Cluster Genealogy` (all four) in
one pass, plus the rank-1 aggregate fractions (group B is rank 1: `retained_
fraction` = 11/12, `main_fragment_fraction` = 6/12, `left_phase_fraction` =
`dispersed_fraction` = 1/12, `fragment_count` = 2), the pooled `relevant_*`
aggregates over all six relevant reference clusters (28 atoms:
`relevant_retained_fraction` = 19/28, `relevant_main_fragment_fraction` = 14/28,
`relevant_left_phase_fraction` = 5/28, `relevant_left_core_fraction` = 2/28,
`relevant_dispersed_fraction` = 1/28, `relevant_vanished_fraction` = 2/28,
`relevant_fragment_count` = 4, `relevant_mean_fragments_per_cluster` = 4/6, plus
the sum-to-one invariant checked directly) and the bidirectional
genealogy aggregates (6 relevant reference clusters: 2 stable, 1 fragmented, 3
disappeared; 5 relevant current clusters: 2 stable, 1 merged, 2 created).
Group C -- reference genealogy Stable while its target cluster is Merged -- is
a direct instance of the asymmetric case documented in "Bidirectional cluster
genealogy", and group D is a direct instance of the "Created" case that
motivated adding the genealogy in the first place (its atoms read as
harmless `Cluster Fate` NotTracked, which is correct but uninformative; only
`Cluster Genealogy` flags that their current cluster is genuinely new).

**Scope.** Covers `AmorphousCarbonTrackingModifier` only. `AmorphousCarbonModifier`'s
own clustering (coordination, connected components, ring detection) has no
C++ test yet -- it carries less risk since it is already cross-checked
against a real 473k-atom Python reference (see "Validation status"), unlike
the tracking modifier's vote/genealogy logic, which previously had no
versioned check at all in either language for the fate/fraction path. The
majority-vote tie-break case from the original hand-built scenario (an
unversioned standalone C++ program predating this harness) is not reproduced
here; this scenario's B-group split (6 vs. 4) exercises the non-tie branch of
the same code, and a tie only changes which `curCid` wins by one comparison.

## How to read the tracking metrics

Two follow-up studies on `camorfo_sp3_60_mCRA` (details in
`contexto_carbono.md`) determined which outputs are safe to quote:

- **Robust to ±5% cutoff variation** (≤1.04 pp spread): `retained_fraction`,
  `left_phase_fraction`, `main_fragment_fraction`, `sp3_percent`.
- **Formerly not robust, fixed**: with the old 1.85 bond cutoff,
  `fragment_count` varied by a factor of 5.5 across ±5%, and
  `largest_cluster_fraction` spread 7.5 pp. Both are stable at the current
  default — see "Why the two cutoffs match by default". Still report the
  cutoffs alongside any cluster count.
- `bond_cutoff` provably does not affect composition (`sp3_percent` is
  identical across bond-cutoff variants), confirming the two cutoffs stay
  independent as designed.

`retained_fraction` and `left_phase_fraction` measure **cumulative identity
decorrelation from the reference**, not net damage. On this dataset they decay
at ~1.5% per cycle and keep doing so after the composition has saturated:
cycles 90→100 give `retained_fraction` 0.8594 with a net `sp3_percent` change
of −0.04 pp, versus 0.8582 for cycles 0→10 where the net change is −3.24 pp.
For net damage, quote `sp3_percent` and `largest_cluster_fraction` instead.

Independently of that: in **sp2** mode quote the `relevant_*` attributes, never
the rank-1 ones. The rank-1 cluster there is 14 atoms, so its fractions are
quantized in steps of 1/14 and carry no statistics (see "Pooled aggregates over
all relevant clusters"). In sp3 the two agree to 5 decimal places, so nothing is
lost by using the pooled form everywhere.
