#!/usr/bin/env python3
"""Precision/recall of the VacancyAnalysisModifier vs raw Wigner-Seitz under noise.

For each synthetic noise case (generate_noise_cases.py) this runs the REAL native
modifier (build/bin/ovito --vacancy-sweep --vacancy-sweep-dump-sites) and scores
the per-site classification against the known vacancy set, for two predictors:

  - raw WS : a site is predicted vacancy iff raw_occupancy == 0
  - method : a site is predicted vacancy iff defect_type_name == "Vacancy"

both compared to the ground-truth vacancy_site_ids. Writes a tidy CSV with
precision/recall/F1 per (sigma, predictor), ready for plotting. The point of the
figure is that raw-WS precision collapses with thermal noise while the method
keeps it high at (ideally) the same recall.
"""

import argparse
import csv
import json
import os
import subprocess
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
OVITO = os.path.join(REPO_ROOT, "build", "bin", "ovito")
DEFAULT_DATASET = os.path.join(REPO_ROOT, "research", "vacancy_analysis",
                               "datasets", "generated", "noise_fcc_8x8x8")
DEFAULT_OUT = os.path.join(REPO_ROOT, "research", "vacancy_analysis", "results",
                           "synthetic", "precision_recall.csv")


def run_case(case_dir, reference, scratch):
    """Run the native modifier on one case, return list of per-site dump rows."""
    counts = os.path.join(scratch, "counts.csv")
    sites = os.path.join(scratch, "sites.csv")
    cmd = [
        OVITO, "--nogui", "--vacancy-sweep",
        "--vacancy-sweep-input", os.path.join(case_dir, "current.xyz"),
        "--vacancy-sweep-reference", reference,
        "--vacancy-sweep-output", counts,
        "--vacancy-sweep-dump-sites", sites,
        "--vacancy-sweep-no-mask",                 # ordered crystal: structural mask N/A
        "--vacancy-sweep-cavity-ratios", "0.5",
        "--vacancy-sweep-recombination-cutoffs", "auto",
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stdout + res.stderr)
        raise RuntimeError(f"ovito failed on {case_dir}")
    with open(sites, newline="") as fh:
        return list(csv.DictReader(fh))


def score(predicted_ids, truth_ids, n_sites):
    tp = len(predicted_ids & truth_ids)
    fp = len(predicted_ids - truth_ids)
    fn = len(truth_ids - predicted_ids)
    precision = tp / (tp + fp) if (tp + fp) else 1.0
    recall = tp / (tp + fn) if (tp + fn) else 1.0
    f1 = 2 * precision * recall / (precision + recall) if (precision + recall) else 0.0
    return dict(tp=tp, fp=fp, fn=fn,
                precision=round(precision, 4), recall=round(recall, 4), f1=round(f1, 4),
                fp_per_1000_sites=round(1000 * fp / n_sites, 3))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", default=DEFAULT_DATASET)
    ap.add_argument("--output", default=DEFAULT_OUT)
    args = ap.parse_args()

    if not os.path.exists(OVITO):
        sys.exit(f"ovito binary not found: {OVITO} (build first)")
    manifest = json.load(open(os.path.join(args.dataset, "cases_manifest.json")))
    reference = os.path.join(args.dataset, manifest["reference_file"])
    n_sites = manifest["reference_site_count"]

    scratch = os.path.join(args.dataset, "_scratch")
    os.makedirs(scratch, exist_ok=True)
    os.makedirs(os.path.dirname(args.output), exist_ok=True)

    fieldnames = ["sigma", "predictor", "precision", "recall", "f1",
                  "tp", "fp", "fn", "fp_per_1000_sites", "n_truth"]
    with open(args.output, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fieldnames)
        w.writeheader()
        for case in manifest["cases"]:
            case_dir = os.path.join(args.dataset, case)
            gt = json.load(open(os.path.join(case_dir, "ground_truth.json")))
            sigma = gt["noise_sigma"]
            truth = set(gt["vacancy_site_ids"])

            rows = run_case(case_dir, reference, scratch)
            raw_pred = {int(r["site_id"]) for r in rows if int(r["raw_occupancy"]) == 0}
            method_pred = {int(r["site_id"]) for r in rows if r["defect_type_name"] == "Vacancy"}

            for predictor, pred in (("raw WS", raw_pred), ("method", method_pred)):
                s = score(pred, truth, n_sites)
                w.writerow({"sigma": sigma, "predictor": predictor, "n_truth": len(truth), **s})
            print(f"sigma {sigma:.2f}: raw WS P/R "
                  f"{score(raw_pred,truth,n_sites)['precision']}/{score(raw_pred,truth,n_sites)['recall']} | "
                  f"method P/R "
                  f"{score(method_pred,truth,n_sites)['precision']}/{score(method_pred,truth,n_sites)['recall']}")

    print(f"# wrote {args.output}")


if __name__ == "__main__":
    main()
