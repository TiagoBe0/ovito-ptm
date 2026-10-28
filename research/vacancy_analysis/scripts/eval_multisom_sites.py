#!/usr/bin/env python3
"""Score MultiSOM clusters of Wigner-Seitz candidates against synthetic ground truth.

Pipeline per case:

  1. run the REAL native modifier
     (build/bin/ovito --vacancy-sweep --vacancy-sweep-features all
      --vacancy-sweep-multisom-dump ...), which writes one LAMMPS-style dump
     holding every WS candidate site with its descriptors, including the
     local deformation context (local_shear_strain_mean, local_rmsd, ...);
  2. run MultiSOM (main.py) over that dump in a scratch working directory,
     using research/vacancy_analysis/multisom/input_params.py as the layer config;
  3. compare the resulting cluster ids against ground_truth.json.

Two things are reported, and the distinction matters:

  * composition   - per cluster: size and the fraction of it that is a real
                    vacancy. This is descriptive and uses no labels to build
                    the clustering, so it is what an unsupervised run gives you.
  * oracle        - precision/recall of the best possible "these clusters are
                    the vacancies" choice for that layer. This is an UPPER BOUND
                    that peeks at the labels to pick the clusters; it says how
                    much separable signal the map found, NOT what an unsupervised
                    deployment would achieve. Never report it as the method's score.

Baselines (raw WS, the rule pipeline, and the GBDT when --ml-model is given) are
written as extra rows so the SOM is read against them and not in isolation.
"""

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
OVITO = os.environ.get("OVITO_BIN", os.path.join(REPO_ROOT, "build-release", "bin", "ovito"))
MULTISOM = os.path.join(REPO_ROOT, "MultiSOM-main")
PARAMS_TEMPLATE = os.path.join(REPO_ROOT, "research", "vacancy_analysis", "multisom", "input_params.py")
DEFAULT_DATASET = os.path.join(REPO_ROOT, "research", "vacancy_analysis",
                               "datasets", "generated", "cascade_fcc_8x8x8")
DEFAULT_OUT = os.path.join(REPO_ROOT, "research", "vacancy_analysis", "results",
                           "synthetic", "multisom_sites.csv")

# Defect Type ids emitted by VacancyAnalysisModifier (see its DefectType enum).
DEFECT_VACANCY = 1


def read_dump(path):
    """Reads a LAMMPS-style dump into a list of dicts, keyed by column name."""
    rows = []
    with open(path) as fh:
        columns = None
        for line in fh:
            if line.startswith("ITEM: ATOMS"):
                columns = line.split()[2:]
                break
        if columns is None:
            raise RuntimeError(f"no 'ITEM: ATOMS' header in {path}")
        for line in fh:
            values = line.split()
            if values:
                rows.append(dict(zip(columns, values)))
    return rows


def run_sweep(case_dir, reference, dump_path, workdir, args):
    counts = os.path.join(workdir, "counts.csv")
    cmd = [
        OVITO, "--nogui", "--vacancy-sweep",
        "--vacancy-sweep-input", os.path.join(case_dir, "current.xyz"),
        "--vacancy-sweep-reference", reference,
        "--vacancy-sweep-output", counts,
        "--vacancy-sweep-no-mask",                  # ordered crystal: structural mask N/A
        "--vacancy-sweep-cavity-ratios", "0.85",
        "--vacancy-sweep-recombination-cutoffs", "auto",
        "--vacancy-sweep-features", "all",
        "--vacancy-sweep-strain-cutoff", str(args.strain_cutoff),
        "--vacancy-sweep-identifier-column", args.identifier_column,
        "--vacancy-sweep-multisom-dump", dump_path,
    ]
    if args.ml_model:
        cmd += ["--vacancy-sweep-ml-model", args.ml_model,
                "--vacancy-sweep-ml-threshold", str(args.ml_threshold)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stdout + res.stderr)
        raise RuntimeError(f"ovito failed on {case_dir}")


def run_multisom(dump_name, workdir):
    """Runs MultiSOM inside workdir and returns the rows of its SOM_* output.

    main.py does `from input_params import PARAMS, LAYERS`, which resolves next to
    main.py itself, and writes its result as 'SOM_' + the file name. So MultiSOM is
    copied into the scratch directory instead of being configured in place - the
    vendored MultiSOM-main tree is left untouched.
    """
    for name in ("main.py", "SOM.py"):
        shutil.copy(os.path.join(MULTISOM, name), os.path.join(workdir, name))

    with open(PARAMS_TEMPLATE) as fh:
        params_src = fh.read()
    params_src = params_src.replace("'file_list': []", f"'file_list': ['{dump_name}']")
    params_src = params_src.replace("'training_file': ''", f"'training_file': '{dump_name}'")
    with open(os.path.join(workdir, "input_params.py"), "w") as fh:
        fh.write(params_src)

    res = subprocess.run([sys.executable, "main.py"], cwd=workdir,
                         capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stdout + res.stderr)
        raise RuntimeError("MultiSOM failed")
    return read_dump(os.path.join(workdir, "SOM_" + dump_name))


def score(predicted, truth):
    tp = len(predicted & truth)
    fp = len(predicted - truth)
    fn = len(truth - predicted)
    precision = tp / (tp + fp) if (tp + fp) else 1.0
    recall = tp / (tp + fn) if (tp + fn) else 1.0
    f1 = 2 * precision * recall / (precision + recall) if (precision + recall) else 0.0
    return dict(tp=tp, fp=fp, fn=fn, precision=round(precision, 4),
                recall=round(recall, 4), f1=round(f1, 4))


def oracle_clusters(rows, layer, truth, base_rate):
    """Best achievable verdict from this layer: keep every cluster purer than the base rate.

    The threshold is the base rate rather than a flat 0.5 because the candidate population is
    heavily imbalanced in the hard cases (at strain level 1.0 only ~6% of the candidates are real
    vacancies), and a majority rule would simply discard everything there and report recall 0
    even when the map separated the artifacts perfectly.

    This peeks at the labels to pick the clusters, so it is an upper bound on the layer's
    separability, not a score an unsupervised deployment could reach.
    """
    members = defaultdict(set)
    for row in rows:
        members[row[layer]].add(row["id"])
    if base_rate >= 1.0:
        # Every candidate is a real vacancy: there is nothing to separate, and no cluster can be
        # purer than the base rate. Keeping everything is the correct verdict, not discarding it.
        return set().union(*members.values()) if members else set(), members
    predicted = set()
    for ids in members.values():
        if len(ids & truth) > base_rate * len(ids):
            predicted |= ids
    return predicted, members


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", default=DEFAULT_DATASET)
    ap.add_argument("--cases", default="", help="Comma-separated case names. Default: all cases in the manifest.")
    ap.add_argument("--output", default=DEFAULT_OUT)
    ap.add_argument("--ml-model", default="", help="Optional GBDT model, adds the ML baseline rows.")
    ap.add_argument("--ml-threshold", type=float, default=0.5)
    ap.add_argument("--strain-cutoff", type=float, default=3.5)
    ap.add_argument("--identifier-column", default="site_id")
    ap.add_argument("--keep-workdir", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(OVITO):
        sys.exit(f"ovito binary not found: {OVITO} (build first)")
    if not os.path.isdir(MULTISOM):
        sys.exit(f"MultiSOM not found: {MULTISOM}")

    manifest = json.load(open(os.path.join(args.dataset, "cases_manifest.json")))
    reference = os.path.join(args.dataset, manifest["reference_file"])
    cases = [c.strip() for c in args.cases.split(",") if c.strip()] or manifest["cases"]
    family = os.path.basename(os.path.normpath(args.dataset))

    fieldnames = ["family", "case", "level", "predictor", "n_candidates", "n_truth",
                  "precision", "recall", "f1", "tp", "fp", "fn", "detail"]
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w", newline="") as out_fh:
        writer = csv.DictWriter(out_fh, fieldnames=fieldnames)
        writer.writeheader()

        for case in cases:
            case_dir = os.path.join(args.dataset, case)
            gt = json.load(open(os.path.join(case_dir, "ground_truth.json")))
            level = gt.get("noise_sigma", gt.get("shear_amplitude_nn", ""))
            truth_all = set(str(i) for i in gt["vacancy_site_ids"])

            workdir = tempfile.mkdtemp(prefix=f"multisom_{case}_")
            try:
                dump_name = "sites.0.dump"
                run_sweep(case_dir, reference, os.path.join(workdir, "sites.*.dump"),
                          workdir, args)
                candidates = read_dump(os.path.join(workdir, dump_name))
                candidate_ids = set(row["id"] for row in candidates)
                # Sites whose atom is genuinely missing but that WS did not even flag cannot be
                # recovered by any post-filter, so the population is scored against the truth
                # restricted to the candidate set, matching how the other predictors are scored.
                truth = truth_all & candidate_ids

                def emit(predictor, predicted, detail=""):
                    row = dict(family=family, case=case, level=level, predictor=predictor,
                               n_candidates=len(candidates), n_truth=len(truth), detail=detail)
                    row.update(score(predicted, truth))
                    writer.writerow(row)
                    print(f"  {predictor:24s} P/R {row['precision']:.3f}/{row['recall']:.3f} "
                          f"(tp {row['tp']} fp {row['fp']} fn {row['fn']}) {detail}")

                print(f"{family}/{case}: {len(candidates)} candidates, "
                      f"{len(truth)} real vacancies among them "
                      f"({len(truth_all) - len(truth)} missed by raw WS)")

                emit("raw_ws", candidate_ids)
                emit("rules_or_ml", set(row["id"] for row in candidates
                                        if int(row["defect_type"]) == DEFECT_VACANCY),
                     detail="ML verdict" if args.ml_model else "rule verdict")

                try:
                    som_rows = run_multisom(dump_name, workdir)
                except RuntimeError as ex:
                    # MultiSOM raises IndexError when a super cluster comes out empty, which happens
                    # on small, homogeneous candidate sets (the easy levels). Record it and carry on
                    # rather than losing the rest of the sweep.
                    print(f"  som                      SKIPPED ({ex})")
                    writer.writerow(dict(family=family, case=case, level=level,
                                         predictor="som", n_candidates=len(candidates),
                                         n_truth=len(truth), detail=f"failed: {ex}"))
                    continue

                base_rate = len(truth) / len(candidates) if candidates else 0.0
                layers = [c for c in som_rows[0] if c.startswith("layer_")]
                for layer in layers:
                    predicted, members = oracle_clusters(som_rows, layer, truth, base_rate)
                    composition = ";".join(
                        f"{cid}:{len(ids)}@{len(ids & truth) / len(ids):.2f}"
                        for cid, ids in sorted(members.items()))
                    emit(f"som_{layer}_oracle", predicted,
                         detail=f"base_rate={base_rate:.2f} {composition}")
            finally:
                if args.keep_workdir:
                    print(f"  workdir kept: {workdir}")
                else:
                    shutil.rmtree(workdir, ignore_errors=True)

    print(f"\n# wrote {args.output}")


if __name__ == "__main__":
    main()
