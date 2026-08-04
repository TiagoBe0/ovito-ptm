#!/usr/bin/env python3
"""Train the vacancy GBDT on native C++ features and export it for the modifier.

Trains the same HistGradientBoostingClassifier as ml_prototype_vacancy_classifier
(--native-features path: only descriptor columns computed by the C++ engine) and
serializes the tree ensemble to a plain JSON file that the native
VacancyAnalysisModifier ML mode can evaluate without any runtime dependency:

    prob = sigmoid(baseline + sum over trees of leaf value)
    node rule: x[feature] <= threshold -> left child, else right child

The JSON records the exact feature order and the sentinel convention (distances
are in units of the estimated nn distance; missing-site sentinels map -1 ->
far_sentinel for distances and -1 -> 0 for counts) so the C++ side and any
future retraining stay in lockstep. A self-check re-evaluates every training
row through the exported representation and asserts bit-level agreement with
sklearn's predict_proba.
"""

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ml_prototype_vacancy_classifier import FAR, FEATURES, collect

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
DEFAULT_MODEL = os.path.join(REPO_ROOT, "research", "vacancy_analysis", "models",
                             "vacancy_gbdt.json")
DEFAULT_TRAIN = "noise_fcc_8x8x8,strain_fcc_8x8x8,cluster_fcc_8x8x8,cascade_fcc_8x8x8"


def export(clf, feature_names, threshold):
    trees = []
    for predictors in clf._predictors:
        nodes = predictors[0].nodes
        trees.append([{
            "leaf": bool(n["is_leaf"]),
            "value": float(n["value"]),
            "feature": int(n["feature_idx"]),
            "threshold": float(n["num_threshold"]),
            "left": int(n["left"]),
            "right": int(n["right"]),
        } for n in nodes])
    return {
        "model": "HistGradientBoostingClassifier",
        "task": "vacancy_candidate_classification",
        "features": list(feature_names),
        "far_sentinel": FAR,
        "baseline": float(np.ravel(clf._baseline_prediction)[0]),
        "threshold": threshold,
        "trees": trees,
    }


def evaluate(model, x):
    raw = model["baseline"]
    for tree in model["trees"]:
        i = 0
        while not tree[i]["leaf"]:
            i = tree[i]["left"] if x[tree[i]["feature"]] <= tree[i]["threshold"] else tree[i]["right"]
        raw += tree[i]["value"]
    return 1.0 / (1.0 + np.exp(-raw))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--train", default=DEFAULT_TRAIN)
    ap.add_argument("--output", default=DEFAULT_MODEL)
    ap.add_argument("--threshold", type=float, default=0.5)
    ap.add_argument("--max-iter", type=int, default=300)
    args = ap.parse_args()

    rows = collect(args.train.split(","), native=True)
    X = np.array([r["features"] for r in rows])
    y = np.array([r["label"] for r in rows])
    print(f"# training on {len(y)} candidate sites ({y.sum()} true vacancies) "
          f"from {args.train}")

    from sklearn.ensemble import HistGradientBoostingClassifier
    clf = HistGradientBoostingClassifier(max_iter=args.max_iter, random_state=0)
    clf.fit(X, y)

    model = export(clf, FEATURES, args.threshold)
    n_nodes = sum(len(t) for t in model["trees"])
    print(f"# exported {len(model['trees'])} trees, {n_nodes} nodes")

    # Self-check: the exported representation must reproduce sklearn exactly.
    probs_sklearn = clf.predict_proba(X)[:, 1]
    probs_exported = np.array([evaluate(model, x) for x in X])
    err = np.abs(probs_sklearn - probs_exported).max()
    print(f"# max |sklearn - exported| over training rows: {err:.3e}")
    if err > 1e-12:
        sys.exit("export self-check FAILED")

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w") as fh:
        json.dump(model, fh)
    print(f"# wrote {args.output} ({os.path.getsize(args.output) // 1024} KiB)")


if __name__ == "__main__":
    main()
