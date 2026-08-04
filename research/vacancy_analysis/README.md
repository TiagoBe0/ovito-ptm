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
