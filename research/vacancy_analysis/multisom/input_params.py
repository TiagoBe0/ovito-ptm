#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""MultiSOM layer configuration for Wigner-Seitz vacancy candidates.

This is a template: eval_multisom_sites.py copies it into a scratch working
directory and rewrites 'file_list' / 'training_file' with the dump produced by

    ovito --vacancy-sweep --vacancy-sweep-features all --vacancy-sweep-multisom-dump ...

Every row is one WS candidate site (raw_occupancy == 0), not one atom, so the
SOM is asked the question the method actually needs answered: which of these
empty sites are real vacancies and which are deformation artifacts?

The layers deliberately separate the three physical signals instead of pooling
them into one distance metric, so a cluster can be read as "cavity-like but
sitting in a sheared region" rather than as an opaque group id:

  1. cavity          - is there really empty space here?
  2. Frenkel context - is there a displaced atom nearby that explains the hole?
  3. deformation     - how deformed is the neighbourhood?

Note on the deformation layer: the sign of its discrimination is NOT universal.
In the synthetic cascade family the artifacts sit in high-shear neighbourhoods,
while in the sinusoidal 'strain' family they sit in LOW-shear ones (the WS cell
crossings happen where the displacement is extremal, i.e. where its gradient
vanishes). A fixed threshold would have to pick one sign and would be wrong on
the other family; that is precisely why this goes to an unsupervised map.
"""

PARAMS = {'file_list': [],
          'search_pattern': 'sites.*.dump',
          'training_file': '',
          'mapping': 'linear'}


LAYERS = [
    # Layer 1 - cavity: distance to the nearest current atoms and how many of
    # them crowd the first shell. A real vacancy leaves a measurable hole.
    {'features': ['knn_d1', 'knn_d2', 'count_100nn'],
     'scaling': ['robust', 'robust', 'robust'],
     'f': 1,
     'sigma': 0.7,
     'eta': 0.5,
     'N': 3,
     'Batched': True,
     'Batch_size': 100},

    # Layer 2 - Frenkel context: an over-occupied site nearby, with surplus to
    # spare, is the signature of an atom that moved rather than one that is gone.
    {'features': ['nearest_interstitial_distance', 'nearest_interstitial_surplus'],
     'scaling': ['robust', 'robust'],
     'f': 1,
     'sigma': 0.7,
     'eta': 0.5,
     'N': 2,
     'Batched': True,
     'Batch_size': 100},

    # Layer 3 - deformation context: the continuous local strain/RMSD/free-volume
    # of the surroundings, which is what the binary structural mask throws away.
    {'features': ['local_shear_strain_mean', 'local_rmsd', 'local_atomic_volume'],
     'scaling': ['robust', 'robust', 'robust'],
     'f': 1,
     'sigma': 0.7,
     'eta': 0.5,
     'N': 2,
     'Batched': True,
     'Batch_size': 100},
]
