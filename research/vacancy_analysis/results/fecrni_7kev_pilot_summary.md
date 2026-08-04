# FeCrNi 7 keV pilot vacancy-analysis sweep

Dataset:

- Reference: `/media/santi/ALMACEN_GRANDE_2/pka_acero/common/reference.FeCrNi`
- Input: `/media/santi/ALMACEN_GRANDE_2/pka_acero/7kev/dump.ballistic.FeCrNi_7keV.*`
- Frames evaluated: first 4 source frames (`timestep = 0, 5000, 10000, 15000`)

Build change:

- Added `modifier/analysis/vacancy/VacancyAnalysisSweepService.cpp` to the
  `Particles` plugin CMake source list.
- Verified `build/bin/ovito --help` exposes `--vacancy-sweep`.

Commands:

```bash
ninja -C build Particles

build/bin/ovito --nogui --vacancy-sweep \
  --vacancy-sweep-input '/media/santi/ALMACEN_GRANDE_2/pka_acero/7kev/dump.ballistic.FeCrNi_7keV.*' \
  --vacancy-sweep-reference /media/santi/ALMACEN_GRANDE_2/pka_acero/common/reference.FeCrNi \
  --vacancy-sweep-output research/vacancy_analysis/results/fecrni_7kev_simple_sweep.csv \
  --vacancy-sweep-mask-signals lammps-c-centro,lammps-c-coord \
  --vacancy-sweep-mask-thresholds 0.3,0.5,0.7 \
  --vacancy-sweep-cavity-ratios 0.4,0.5 \
  --vacancy-sweep-recombination-cutoffs auto \
  --vacancy-sweep-max-frames 4

build/bin/ovito --nogui --vacancy-sweep \
  --vacancy-sweep-input '/media/santi/ALMACEN_GRANDE_2/pka_acero/7kev/dump.ballistic.FeCrNi_7keV.*' \
  --vacancy-sweep-reference /media/santi/ALMACEN_GRANDE_2/pka_acero/common/reference.FeCrNi \
  --vacancy-sweep-output research/vacancy_analysis/results/fecrni_7kev_nomask_baseline.csv \
  --vacancy-sweep-no-mask \
  --vacancy-sweep-cavity-ratios 0.4,0.5 \
  --vacancy-sweep-recombination-cutoffs auto \
  --vacancy-sweep-max-frames 4
```

Outputs:

- `fecrni_7kev_smoke.csv`: 2 rows, single-parameter import smoke test.
- `fecrni_7kev_simple_sweep.csv`: 48 rows, all `success`.
- `fecrni_7kev_nomask_baseline.csv`: 8 rows, all `success`.

Quick results:

```text
timestep  raw WS V/I  no-mask V/I after Frenkel  masked V range
0         0/0         0/0                       0..0
5000      361/361     181/181                   0..23
10000     378/378     178/178                   0..63
15000     94/94       47/47                     0..47
```

Notes:

- `frenkel_recombined` removed 180, 200, and 47 local Frenkel-compatible pairs at
  timesteps 5000, 10000, and 15000.
- `freevolume_excluded` was 0 for cavity ratios 0.4 and 0.5 in this pilot.
- `lammps-c-centro` classified all remaining vacancies as structural for the
  nonzero timesteps in this pilot.
- `lammps-c-coord` was less aggressive; with threshold 0.7, the maximum retained
  vacancy counts were 23, 63, and 47 at timesteps 5000, 10000, and 15000.
- Cross-check pending: raw WS counts differ from `/media/santi/ALMACEN_GRANDE_2/pka_acero/results/7kev.csv`
  by a few sites at some timesteps (for example 361 vs 362 at timestep 5000).
  This should be investigated before using the old CSV as a strict baseline.

Cross-check update:

- The legacy script `common/analyze_cascade.py` does not use
  `common/reference.FeCrNi` unless passed explicitly. By default it uses the
  first ballistic frame in the run directory as the reference:
  `/media/santi/ALMACEN_GRANDE_2/pka_acero/7kev/dump.ballistic.FeCrNi_7keV.0`.
- Re-running the legacy script for the first 4 frames reproduced the old CSV:
  `0, 362, 387, 93` vacancies at timesteps `0, 5000, 10000, 15000`.
- Running the new native sweep with the same run-local reference reproduced
  those raw WS counts exactly.
- The previous mismatch came from using `common/reference.FeCrNi` as the native
  sweep reference. That LAMMPS data file has a larger cell (`L = 247.71`) than
  the 7 keV ballistic dumps (`L = 243.738892864`), so it is a different
  reference state for WS assignment.
- Comparison table: `fecrni_7kev_crosscheck_counts.csv`.
