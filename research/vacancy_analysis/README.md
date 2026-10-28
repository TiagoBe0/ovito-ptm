# Vacancy Analysis research harness

This folder contains the first reproducible validation assets for the
`VacancyAnalysisModifier` project.

## Layout

    docs/       CONTEXTO_vacancy.md            project log: method, decisions, validations
                CONTEXTO_testing_database.md   how to use the FeCrNi PKA cascade database
    scripts/    data generation, validation and sweep automation
    datasets/   generated synthetic cases (NOT versioned, regenerate with scripts/)
    models/     trained GBDT classifier (vacancy_gbdt.json)
    results/    CSV outputs, figures and sweep summaries
    report/     paper material


The initial goal is deliberately small:

1. Generate a perfect FCC reference crystal.
2. Generate three current configurations with known ground truth:
   - `perfect`
   - `single_vacancy`
   - `near_frenkel_pair`
3. Validate the raw Wigner-Seitz counts and the local Frenkel recombination
   expected from these synthetic cases.

These assets are meant to support the scientific paper workflow. They are not
full OVITO integration tests yet.

## Generate datasets

```bash
python3 research/vacancy_analysis/scripts/generate_synthetic_cases.py
```

By default this writes files to:

```text
research/vacancy_analysis/datasets/generated/fcc_4x4x4/
```

Each case contains:

- `reference.xyz`
- `current.xyz`
- `ground_truth.json`

The XYZ files use extended XYZ metadata with a periodic orthorhombic cell.

## Validate generated cases

```bash
python3 research/vacancy_analysis/scripts/validate_synthetic_cases.py \
  research/vacancy_analysis/datasets/generated/fcc_4x4x4
```

The validator performs a small standalone Wigner-Seitz assignment and Frenkel
pair recombination check. It verifies that the generated files have the expected
raw and corrected counts.

## Native modifier boundary

`VacancyAnalysisModifier` is a native C++/Qt OVITO modifier. Python scripts in
this folder are validation and data-generation utilities only; the modifier does
not depend on Python at runtime.

## Next validation step

The next validation layer should run the actual OVITO modifier and compare
its output properties/attributes against the `ground_truth.json` files:

- `Raw Occupancy`
- `Corrected Interstitial Surplus`
- `Defect Type`
- `Vacancy Confidence`
- `Nearest Atom Distance`
- `Disorder Fraction`
- `VacancyAnalysis.vacancy_count`
- `VacancyAnalysis.interstitial_count`
- `VacancyAnalysis.raw_ws_vacancies`
- `VacancyAnalysis.raw_ws_interstitials`
- `VacancyAnalysis.frenkel_recombined`
- `VacancyAnalysis.estimated_nn_distance`
- `VacancyAnalysis.mask_signal_used`
- `VacancyAnalysis.mask_signal_name`
- `VacancyAnalysis.mask_signal_cutoff`

At the time this harness was updated, the local build provided
`build/bin/ovito --nogui`, but no importable `ovito` Python module in the system
Python environment. Any batch automation added here should remain an external
validation utility, not part of the modifier implementation.

## Local deformation context and MultiSOM

The modifier emits a continuous description of how deformed the neighbourhood of
each Wigner-Seitz candidate is, instead of only the binary structural mask:
per-site properties `Local Shear Strain` (mean + max), `Local Volumetric Strain`,
`Local RMSD`, `Local Atomic Volume` (normalized by its own mean) and
`Local Context Count` (how many current atoms the aggregates average over, `-1`
on non-candidate sites). The `VacancyAnalysis.local_signals` attribute lists which
of them were actually available.

They are aggregated over the 1.5 nn shell of the site (first shell included; `Disorder Fraction` uses the 1.3–2.1 nn annulus instead),
and they are read from whatever upstream modifier provides `Shear Strain`,
`Volumetric Strain`, `RMSD` and `Atomic Volume`. The sweep can compute those itself,
so no LAMMPS computes and no re-run of the simulation are needed:

    build/bin/ovito --nogui --vacancy-sweep \
        --vacancy-sweep-input  <case>/current.xyz \
        --vacancy-sweep-reference <reference.xyz> \
        --vacancy-sweep-output counts.csv \
        --vacancy-sweep-features all \        # ptm, strain, voronoi
        --vacancy-sweep-strain-cutoff 3.5 \
        --vacancy-sweep-identifier-column site_id \
        --vacancy-sweep-multisom-dump sites.*.dump

`--vacancy-sweep-identifier-column` is needed whenever the files carry no `id`
column: AtomicStrain maps atoms to reference sites by identifier, which is the
only mapping that survives atoms being missing. The `*` in the dump path is
replaced by the evaluation index (one file per evaluation, which is what MultiSOM
consumes). By default only the WS candidates are written; add
`--vacancy-sweep-multisom-all-sites` for the full site set.

`scripts/eval_multisom_sites.py` runs that sweep, feeds the dump to MultiSOM with
the layer configuration in `multisom/input_params.py` (cavity / Frenkel context /
deformation context) and scores the resulting clusters against the synthetic
ground truth. Results: `results/synthetic/multisom_sites_{cascade,strain}.csv`.

Two caveats when reading those CSVs:

- the `som_layer_N_oracle` rows are an **upper bound**, not a score: the choice of
  which clusters count as vacancies is made by looking at the labels. The `detail`
  column carries the honest, label-free part — the size and vacancy purity of every
  cluster. A cluster like `0.25:101@0.00` is 101 candidates of which none is a real
  vacancy, i.e. an artifact cluster the map found on its own;
- MultiSOM raises an `IndexError` when a super cluster comes out empty, which
  happens on small homogeneous candidate sets (e.g. `cascade level_0.15`). Those
  cases are recorded as `predictor=som, detail=failed: ...` and skipped.

**Where this helps:** in the sinusoidal `strain` family, where the rule pipeline
collapses, the SOM beats it on both precision and recall (level 0.60: rules
0.47/0.71 vs 0.63/0.92; level 0.80: 0.17/0.48 vs 0.27/0.74). In the `cascade`
family it is roughly on par with the rules.

**Do not turn the deformation context into a fixed threshold.** Its discriminating
sign is not universal: in the cascade family the artifacts sit in *high*-shear
neighbourhoods (mean 0.167 vs 0.058 for real vacancies), while in the sinusoidal
strain family they sit in *low*-shear ones (0.077 vs 0.115), because the WS cell
crossings happen where the displacement is extremal and its gradient therefore
vanishes. Any single threshold is wrong on one of the two families, which is the
argument for handing these descriptors to a learned or unsupervised model.

## FeCrNi cascade datasets

The real radiation-damage data in `/media/santi/ALMACEN_GRANDE_2/pka_acero`
is suitable for the next validation layer. These LAMMPS dumps include imported
per-atom columns such as `c_centro` and `c_coord`, which the modifier can now
use directly as structural-mask signals:

- `LAMMPS c_centro`: disordered atoms are those above an automatic `mean + std`
  cutoff.
- `LAMMPS c_coord`: disordered atoms are those below an automatic `mean - std`
  cutoff.

These datasets should be treated as realistic benchmarking/comparison cases
rather than absolute ground truth. The synthetic cases remain the ground-truth
tests.
