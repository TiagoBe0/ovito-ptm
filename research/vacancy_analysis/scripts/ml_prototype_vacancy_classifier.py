#!/usr/bin/env python3
"""ML prototype: learned per-site vacancy classifier vs the rule-based method.

Viability test for replacing the fixed-threshold consensus stages (Frenkel
recombination + cavity + mask) with a learned classifier. For every synthetic
noise case this runs the REAL native modifier (build/bin/ovito --vacancy-sweep
--vacancy-sweep-dump-sites), then builds one feature row per WS candidate site
(raw_occupancy == 0) combining:

  - the C++ per-site dump columns (nearest_atom_distance, surplus, frenkel
    partner, disorder fraction), and
  - neighborhood descriptors computed here with periodic KDTrees (distances to
    the k nearest current atoms, shell counts, distance/count of other empty
    sites, distance/surplus of the nearest over-occupied site).

The Python-side descriptors are a stand-in for features the C++ engine would
export natively if the prototype pays off; the classification target is
whether the site is in the generator's ground-truth vacancy set.

Train: all sigma cases of one dataset (default seed-42 noise_fcc_8x8x8).
Test:  all sigma cases of a DIFFERENT-seed dataset (different vacancy sites and
noise realizations), scored per sigma against raw WS and the rule method.

Outputs results/synthetic/ml_prototype_metrics.csv and prints a comparison
table plus permutation feature importances.
"""

import argparse
import csv
import json
import os
import subprocess
import sys

import numpy as np
from scipy.spatial import cKDTree

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
OVITO = os.path.join(REPO_ROOT, "build", "bin", "ovito")
DATASETS = os.path.join(REPO_ROOT, "research", "vacancy_analysis", "datasets", "generated")
DEFAULT_OUT = os.path.join(REPO_ROOT, "research", "vacancy_analysis", "results",
                           "synthetic", "ml_prototype_metrics.csv")

KNN = 6  # distances to the k nearest current atoms used as features

FEATURES = (
    [f"d{i+1}_nn" for i in range(KNN)]
    + ["count_075nn", "count_100nn", "count_125nn",
       "surplus_self", "disorder_fraction", "has_frenkel_partner",
       "d_empty_nn", "n_empty_150nn",
       "d_overocc_nn", "surplus_overocc", "n_overocc_150nn"]
)


def read_extxyz(path):
    """Return (positions Nx3 float, site_ids N int) from the generator's extxyz."""
    with open(path) as fh:
        n = int(fh.readline())
        fh.readline()  # header
        pos = np.empty((n, 3))
        sid = np.empty(n, dtype=np.int64)
        for i in range(n):
            parts = fh.readline().split()
            pos[i] = [float(parts[1]), float(parts[2]), float(parts[3])]
            sid[i] = int(parts[4])
    return pos, sid


def run_native(case_dir, reference, sites_csv):
    """Run the native modifier once, producing the per-site dump CSV."""
    cmd = [
        OVITO, "--nogui", "--vacancy-sweep",
        "--vacancy-sweep-input", os.path.join(case_dir, "current.xyz"),
        "--vacancy-sweep-reference", reference,
        "--vacancy-sweep-output", os.path.join(os.path.dirname(sites_csv), "counts.csv"),
        "--vacancy-sweep-dump-sites", sites_csv,
        "--vacancy-sweep-no-mask",
        "--vacancy-sweep-cavity-ratios", "0.5",
        "--vacancy-sweep-recombination-cutoffs", "auto",
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stdout + res.stderr)
        raise RuntimeError(f"ovito failed on {case_dir}")


FAR = 5.0  # sentinel distance (in nn units) when there is no such site


def native_features(r, nn):
    """Assemble the feature vector from the C++-computed dump columns only."""
    def dist(col):
        d = float(r[col])
        return d / nn if d >= 0 else FAR

    def count(col):
        c = int(r[col])
        return max(c, 0)

    feats = [dist(f"knn_d{i+1}") for i in range(KNN)]
    feats += [count("count_075nn"), count("count_100nn"), count("count_125nn")]
    feats.append(int(r["corrected_interstitial_surplus"]))
    feats.append(float(r["disorder_fraction"] or 0.0))
    feats.append(1.0 if int(r["frenkel_partner_site"]) >= 0 else 0.0)
    feats.append(dist("nearest_vacant_distance"))
    feats.append(count("vacant_neighbors_150"))
    feats.append(dist("nearest_interstitial_distance"))
    feats.append(count("nearest_interstitial_surplus"))
    feats.append(count("interstitial_neighbors_150"))
    return feats


def case_rows(dataset_dir, case, manifest, force=False, native=False):
    """Feature rows for the WS-candidate sites of one sigma case."""
    case_dir = os.path.join(dataset_dir, case)
    gt = json.load(open(os.path.join(case_dir, "ground_truth.json")))
    truth = set(gt["vacancy_site_ids"])
    nn = manifest["nn_distance"]
    cell = np.array(manifest["cell"])

    scratch = os.path.join(dataset_dir, "_scratch", "ml", case)
    os.makedirs(scratch, exist_ok=True)
    sites_csv = os.path.join(scratch, "sites.csv")
    if force or not os.path.exists(sites_csv):
        run_native(case_dir, os.path.join(dataset_dir, manifest["reference_file"]), sites_csv)

    with open(sites_csv, newline="") as fh:
        dump = {int(r["site_id"]): r for r in csv.DictReader(fh)}

    if native:
        return [{
            "sigma": gt["noise_sigma"], "site_id": s,
            "label": int(s in truth),
            "rules_pred": int(r["defect_type_name"] == "Vacancy"),
            "features": native_features(r, nn),
        } for s, r in dump.items() if int(r["raw_occupancy"]) == 0]

    ref_pos, ref_sid = read_extxyz(os.path.join(dataset_dir, manifest["reference_file"]))
    site_pos = {int(s): ref_pos[i] for i, s in enumerate(ref_sid)}
    cur_pos, _ = read_extxyz(os.path.join(case_dir, "current.xyz"))
    atoms_tree = cKDTree(np.mod(cur_pos, cell), boxsize=cell)

    empty_ids = [s for s, r in dump.items() if int(r["raw_occupancy"]) == 0]
    over_ids = [s for s, r in dump.items() if int(r["raw_occupancy"]) >= 2]
    empty_xyz = np.array([site_pos[s] for s in empty_ids]).reshape(-1, 3)
    over_xyz = np.array([site_pos[s] for s in over_ids]).reshape(-1, 3)
    empty_tree = cKDTree(np.mod(empty_xyz, cell), boxsize=cell) if empty_ids else None
    over_tree = cKDTree(np.mod(over_xyz, cell), boxsize=cell) if over_ids else None
    far = FAR

    rows = []
    for s in empty_ids:
        r = dump[s]
        p = np.mod(site_pos[s], cell)
        dists, _ = atoms_tree.query(p, k=KNN)
        feats = list(np.asarray(dists) / nn)
        for radius in (0.75, 1.0, 1.25):
            feats.append(len(atoms_tree.query_ball_point(p, radius * nn)))
        feats.append(int(r["corrected_interstitial_surplus"]))
        feats.append(float(r["disorder_fraction"] or 0.0))
        feats.append(1.0 if int(r["frenkel_partner_site"]) >= 0 else 0.0)

        d_empty, _ = empty_tree.query(p, k=2)  # k=1 is the site itself
        feats.append(d_empty[1] / nn if len(empty_ids) > 1 else far)
        feats.append(len(empty_tree.query_ball_point(p, 1.5 * nn)) - 1)

        if over_tree is not None:
            d_over, j = over_tree.query(p, k=1)
            feats.append(d_over / nn)
            feats.append(int(dump[over_ids[j]]["corrected_interstitial_surplus"]))
            feats.append(len(over_tree.query_ball_point(p, 1.5 * nn)))
        else:
            feats.extend([far, 0, 0])

        rows.append({
            "sigma": gt["noise_sigma"], "site_id": s,
            "label": int(s in truth),
            "rules_pred": int(r["defect_type_name"] == "Vacancy"),
            "features": feats,
        })
    return rows


def collect(dataset_names, force=False, native=False):
    rows = []
    for dataset_name in dataset_names:
        dataset_dir = os.path.join(DATASETS, dataset_name)
        manifest = json.load(open(os.path.join(dataset_dir, "cases_manifest.json")))
        for case in manifest["cases"]:
            for row in case_rows(dataset_dir, case, manifest, force=force, native=native):
                row["dataset"] = dataset_name
                rows.append(row)
    return rows


def prf(pred, label):
    tp = int(np.sum((pred == 1) & (label == 1)))
    fp = int(np.sum((pred == 1) & (label == 0)))
    fn = int(np.sum((pred == 0) & (label == 1)))
    p = tp / (tp + fp) if tp + fp else 1.0
    r = tp / (tp + fn) if tp + fn else 1.0
    f1 = 2 * p * r / (p + r) if p + r else 0.0
    return p, r, f1, tp, fp, fn


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--train", default="noise_fcc_8x8x8",
                    help="comma-separated training dataset names under datasets/generated/")
    ap.add_argument("--test", default="noise_fcc_8x8x8_seed7",
                    help="comma-separated held-out dataset names")
    ap.add_argument("--output", default=DEFAULT_OUT)
    ap.add_argument("--force", action="store_true", help="re-run the native tool even if dumps are cached")
    ap.add_argument("--native-features", action="store_true",
                    help="use only the C++-computed descriptor columns from the site dump "
                         "(no Python KDTree features); requires dumps from a binary that emits them")
    args = ap.parse_args()

    if not os.path.exists(OVITO):
        sys.exit(f"ovito binary not found: {OVITO} (build first)")

    print(f"# collecting train={args.train} test={args.test} "
          f"({'native C++' if args.native_features else 'python'} features)")
    train_rows = collect(args.train.split(","), force=args.force, native=args.native_features)
    test_rows = collect(args.test.split(","), force=args.force, native=args.native_features)
    Xtr = np.array([r["features"] for r in train_rows])
    ytr = np.array([r["label"] for r in train_rows])
    Xte = np.array([r["features"] for r in test_rows])
    yte = np.array([r["label"] for r in test_rows])
    print(f"# train: {len(ytr)} candidate sites ({ytr.sum()} true vacancies) | "
          f"test: {len(yte)} candidates ({yte.sum()} true)")

    from sklearn.ensemble import HistGradientBoostingClassifier
    from sklearn.inspection import permutation_importance
    clf = HistGradientBoostingClassifier(max_iter=300, random_state=0)
    clf.fit(Xtr, ytr)
    prob = clf.predict_proba(Xte)[:, 1]

    te_sigma = np.array([r["sigma"] for r in test_rows])
    te_dataset = np.array([r["dataset"] for r in test_rows])
    rules = np.array([r["rules_pred"] for r in test_rows])
    ml = (prob >= 0.5).astype(int)
    raw = np.ones(len(yte), dtype=int)  # raw WS counts every candidate

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["dataset", "sigma", "predictor", "precision",
                                           "recall", "f1", "tp", "fp", "fn"])
        w.writeheader()

        def report(dataset, sigma_label, m):
            line = {}
            for name, pr in (("raw WS", raw[m]), ("rules", rules[m]), ("ml", ml[m])):
                p, r, f1, tp, fp, fn = prf(pr, yte[m])
                w.writerow({"dataset": dataset, "sigma": sigma_label, "predictor": name,
                            "precision": round(p, 4), "recall": round(r, 4),
                            "f1": round(f1, 4), "tp": tp, "fp": fp, "fn": fn})
                line[name] = (p, r, f1)
            print(f"{sigma_label:>6} | {line['raw WS'][0]:>5.2f} / {line['raw WS'][1]:.2f} | "
                  f"{line['rules'][0]:>5.2f} / {line['rules'][1]:.2f} | "
                  f"{line['ml'][0]:>5.2f} / {line['ml'][1]:.2f} | {line['ml'][2]:.3f}")

        for ds in sorted(set(te_dataset)):
            print(f"\n## {ds}")
            print(f"{'level':>6} | {'raw WS P/R':>13} | {'rules P/R':>13} | {'ML P/R':>13} | ML F1")
            for sg in sorted({r["sigma"] for r in test_rows if r["dataset"] == ds}):
                report(ds, f"{sg:.2f}", (te_dataset == ds) & (te_sigma == sg))
            report(ds, "all", te_dataset == ds)

    imp = permutation_importance(clf, Xte, yte, n_repeats=10, random_state=0, scoring="f1")
    order = np.argsort(-imp.importances_mean)
    print("\n# permutation importance (test set, F1):")
    for i in order:
        if imp.importances_mean[i] <= 0:
            continue
        print(f"  {FEATURES[i]:>18}: {imp.importances_mean[i]:+.4f} ± {imp.importances_std[i]:.4f}")
    print(f"\n# wrote {args.output}")


if __name__ == "__main__":
    main()
