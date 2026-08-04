#!/usr/bin/env python3
"""Drive the native VacancyAnalysis sweep over the FeCrNi PKA database.

Implements task #1 / #2 from CONTEXTO_testing_database.md: automate the
table `energy -> reference -> input glob -> output csv` and run the compact
production grid across all available cascade energies.

Key decisions (see pilot cross-check):
- The reference is ALWAYS the run-local first ballistic frame, not
  common/reference.FeCrNi (which has a different cell and changes WS counts).
- Each energy has its own file-naming convention, so the reference is found
  as the lowest-numbered ballistic frame in the run directory.
- 2kev / 3kev do not start at timestep 0; their lowest frame is used as the
  reference but flagged as `needs_review` because it may not be a clean
  pre-damage state.

Two sweeps are run per energy:
  1. masked grid:  signals {lammps-c-coord, lammps-c-centro} x thresholds
                   {0.3,0.5,0.7}, cavity 0.5, recomb auto  -> 6 rows/frame
  2. no-mask line: cavity 0.5, recomb auto                 -> 1 row/frame

With --ml-model a third sweep is added per energy (and the masked grid can be
skipped with --skip-masked to iterate faster):
  3. ML line: no mask, learned GBDT verdict (vacancy_count = ML count)
     -> <energy>_ml.csv
"""

import argparse
import glob
import os
import re
import subprocess
import sys

PKA_ROOT = "/media/santi/ALMACEN_GRANDE_2/pka_acero"
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
OVITO = os.path.join(REPO_ROOT, "build", "bin", "ovito")
RESULTS_DIR = os.path.join(REPO_ROOT, "research", "vacancy_analysis", "results", "energy_sweep")

# energy -> (subdir, ballistic-file prefix). The glob is "<prefix>.*" and the
# reference is the file whose trailing integer is smallest.
ENERGIES = {
    "1kev":     ("1kev",     "dump.ballistic_1kev.FeCrNi_1kev"),
    "2kev":     ("2kev",     "dump.ballistic_2kev.FeCrNi_2kev_s"),     # needs_review ref
    "3kev":     ("3kev",     "dump.ballistic_3kev.FeCrNi_3kev"),       # needs_review ref
    "4kev":     ("4kev",     "dump.ballistic.FeCrNi_4keV"),
    "5kev":     ("5kev",     "dump.ballistic_5kev.FeCrNi_5kev_v2"),
    "6kev":     ("6kev",     "dump.ballistic.FeCrNi_6keV"),
    "6kev_it1": ("6kev_it1", "dump.ballistic.FeCrNi_6keV"),
    "6kev_it2": ("6kev_it2", "dump.ballistic.FeCrNi_6keV_it2"),
    "7kev":     ("7kev",     "dump.ballistic.FeCrNi_7keV"),
    "8kev_real":("8kev_real","dump.ballistic.FeCrNi_8keV"),
}

NEEDS_REVIEW_REF = {"2kev", "3kev"}


def find_reference(subdir, prefix):
    """Return the lowest-numbered ballistic frame for a run, or None."""
    pattern = os.path.join(PKA_ROOT, subdir, prefix + ".*")
    frames = []
    for path in glob.glob(pattern):
        m = re.search(r"\.(\d+)$", path)
        if m:
            frames.append((int(m.group(1)), path))
    if not frames:
        return None
    frames.sort()
    return frames[0][1]


def run_sweep(input_glob, reference, output, masked, max_frames, dry_run,
              ml_model=None, ml_threshold=0.5):
    cmd = [
        OVITO, "--nogui", "--vacancy-sweep",
        "--vacancy-sweep-input", input_glob,
        "--vacancy-sweep-reference", reference,
        "--vacancy-sweep-output", output,
        "--vacancy-sweep-cavity-ratios", "0.5",
        "--vacancy-sweep-recombination-cutoffs", "auto",
    ]
    if masked:
        cmd += [
            "--vacancy-sweep-mask-signals", "lammps-c-coord,lammps-c-centro",
            "--vacancy-sweep-mask-thresholds", "0.3,0.5,0.7",
        ]
    else:
        cmd += ["--vacancy-sweep-no-mask"]
    if ml_model:
        cmd += ["--vacancy-sweep-ml-model", ml_model,
                "--vacancy-sweep-ml-threshold", str(ml_threshold)]
    if max_frames is not None:
        cmd += ["--vacancy-sweep-max-frames", str(max_frames)]

    if dry_run:
        print("  DRY:", " ".join(cmd))
        return True
    print("  RUN:", os.path.basename(output))
    res = subprocess.run(cmd, capture_output=True, text=True)
    tail = (res.stdout + res.stderr).strip().splitlines()
    for line in tail[-3:]:
        print("    ", line)
    return res.returncode == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--energies", nargs="*", default=list(ENERGIES),
                    help="Subset of energies to run (default: all).")
    ap.add_argument("--max-frames", type=int, default=None,
                    help="Limit frames per run (default: all frames).")
    ap.add_argument("--dry-run", action="store_true",
                    help="Print commands without executing.")
    ap.add_argument("--ml-model", default=None,
                    help="GBDT model JSON: adds an ML-verdict sweep (<energy>_ml.csv) per energy.")
    ap.add_argument("--ml-threshold", type=float, default=0.5)
    ap.add_argument("--skip-masked", action="store_true",
                    help="Skip the (slow) masked grid; run only no-mask and, if requested, ML.")
    ap.add_argument("--skip-nomask", action="store_true",
                    help="Skip the no-mask baseline (it already exists from a previous pass).")
    args = ap.parse_args()

    if not os.path.exists(OVITO):
        sys.exit(f"ovito binary not found: {OVITO} (build first)")
    os.makedirs(RESULTS_DIR, exist_ok=True)

    print(f"# results -> {RESULTS_DIR}")
    failures = []
    for name in args.energies:
        if name not in ENERGIES:
            print(f"!! unknown energy {name}, skipping")
            continue
        subdir, prefix = ENERGIES[name]
        reference = find_reference(subdir, prefix)
        if reference is None:
            print(f"!! {name}: no ballistic frames found, skipping")
            failures.append(name)
            continue
        flag = "  [needs_review ref]" if name in NEEDS_REVIEW_REF else ""
        input_glob = os.path.join(PKA_ROOT, subdir, prefix + ".*")
        print(f"\n== {name}{flag}\n   ref: {os.path.basename(reference)}")

        ok1 = args.skip_masked or run_sweep(input_glob, reference,
                        os.path.join(RESULTS_DIR, f"{name}_masked_grid.csv"),
                        masked=True, max_frames=args.max_frames, dry_run=args.dry_run)
        ok2 = args.skip_nomask or run_sweep(input_glob, reference,
                        os.path.join(RESULTS_DIR, f"{name}_nomask.csv"),
                        masked=False, max_frames=args.max_frames, dry_run=args.dry_run)
        ok3 = True
        if args.ml_model:
            ok3 = run_sweep(input_glob, reference,
                            os.path.join(RESULTS_DIR, f"{name}_ml.csv"),
                            masked=False, max_frames=args.max_frames, dry_run=args.dry_run,
                            ml_model=os.path.abspath(args.ml_model), ml_threshold=args.ml_threshold)
        if not (ok1 and ok2 and ok3):
            failures.append(name)

    print("\n# done." + (f" FAILURES: {failures}" if failures else " all ok."))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
