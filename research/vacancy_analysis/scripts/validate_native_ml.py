#!/usr/bin/env python3
"""End-to-end validation of the native ML classification mode.

Runs the REAL binary with --vacancy-sweep-ml-model on every case of the
held-out datasets and checks, per candidate site:

  1. numerical agreement: the ml_probability column written by the C++
     evaluator must match a pure-Python evaluation of the exported JSON model
     on the native feature columns of the same dump row;
  2. classification quality: the native verdict (defect_type_name == "Vacancy"
     in ML mode) scored against the synthetic ground truth, per sweep level.

This closes the loop: after this check passes, every number in the ML-mode
sweep comes from the C++ binary alone, with Python only used for training.
"""

import argparse
import csv
import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from export_ml_model import DEFAULT_MODEL, evaluate
from ml_prototype_vacancy_classifier import DATASETS, OVITO, native_features, prf

DEFAULT_TEST = ("noise_fcc_8x8x8_seed7,strain_fcc_8x8x8_seed7,"
                "cluster_fcc_8x8x8_seed7,cascade_fcc_8x8x8_seed7")
DEFAULT_OUT = os.path.join(os.path.dirname(DEFAULT_MODEL), "..", "results",
                           "synthetic", "ml_native_mode_metrics.csv")


def run_case_ml(case_dir, reference, sites_csv, model_path, threshold):
    cmd = [
        OVITO, "--nogui", "--vacancy-sweep",
        "--vacancy-sweep-input", os.path.join(case_dir, "current.xyz"),
        "--vacancy-sweep-reference", reference,
        "--vacancy-sweep-output", os.path.join(os.path.dirname(sites_csv), "counts.csv"),
        "--vacancy-sweep-dump-sites", sites_csv,
        "--vacancy-sweep-no-mask",
        "--vacancy-sweep-cavity-ratios", "0.85",
        "--vacancy-sweep-recombination-cutoffs", "auto",
        "--vacancy-sweep-ml-model", model_path,
        "--vacancy-sweep-ml-threshold", str(threshold),
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stdout + res.stderr)
        raise RuntimeError(f"ovito failed on {case_dir}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument("--test", default=DEFAULT_TEST)
    ap.add_argument("--threshold", type=float, default=0.5)
    ap.add_argument("--output", default=os.path.normpath(DEFAULT_OUT))
    args = ap.parse_args()

    model = json.load(open(args.model))
    max_prob_err, n_checked, decision_mismatch = 0.0, 0, 0

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    out = open(args.output, "w", newline="")
    w = csv.DictWriter(out, fieldnames=["dataset", "sigma", "precision", "recall",
                                        "f1", "tp", "fp", "fn"])
    w.writeheader()

    for dataset in args.test.split(","):
        dataset_dir = os.path.join(DATASETS, dataset)
        manifest = json.load(open(os.path.join(dataset_dir, "cases_manifest.json")))
        reference = os.path.join(dataset_dir, manifest["reference_file"])
        nn = manifest["nn_distance"]
        print(f"\n## {dataset}")
        for case in manifest["cases"]:
            case_dir = os.path.join(dataset_dir, case)
            gt = json.load(open(os.path.join(case_dir, "ground_truth.json")))
            truth = set(gt["vacancy_site_ids"])

            scratch = os.path.join(dataset_dir, "_scratch", "ml_native", case)
            os.makedirs(scratch, exist_ok=True)
            sites_csv = os.path.join(scratch, "sites.csv")
            run_case_ml(case_dir, reference, sites_csv, args.model, args.threshold)

            with open(sites_csv, newline="") as fh:
                rows = [r for r in csv.DictReader(fh) if int(r["raw_occupancy"]) == 0]

            pred, label = [], []
            for r in rows:
                # 1. numerical agreement of the native evaluator with the exported model
                prob_cpp = float(r["ml_probability"])
                prob_py = evaluate(model, np.array(native_features(r, nn)))
                max_prob_err = max(max_prob_err, abs(prob_cpp - prob_py))
                n_checked += 1
                if (prob_cpp >= args.threshold) != (prob_py >= args.threshold):
                    decision_mismatch += 1
                # 2. native verdict vs ground truth
                pred.append(int(r["defect_type_name"] == "Vacancy"))
                label.append(int(int(r["site_id"]) in truth))

            p, rr, f1, tp, fp, fn = prf(np.array(pred), np.array(label))
            w.writerow({"dataset": dataset, "sigma": gt["noise_sigma"],
                        "precision": round(p, 4), "recall": round(rr, 4),
                        "f1": round(f1, 4), "tp": tp, "fp": fp, "fn": fn})
            print(f"  {gt['noise_sigma']:>5.2f}: native ML P/R {p:.2f}/{rr:.2f} "
                  f"(tp={tp} fp={fp} fn={fn})")

    out.close()
    print(f"\n# probability agreement over {n_checked} candidate sites: "
          f"max |C++ - Python| = {max_prob_err:.3e}, decision mismatches = {decision_mismatch}")
    print(f"# wrote {args.output}")
    if max_prob_err > 1e-6 or decision_mismatch:
        sys.exit("NATIVE ML VALIDATION FAILED")


if __name__ == "__main__":
    main()
