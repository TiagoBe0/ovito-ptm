#!/usr/bin/env python3
"""Metricas de exactitud de seguimiento del tracker contra una verdad de terreno conocida.

Compara el CSV de asignaciones que escribe el runner headless
(`<base>_assignments.csv`: frame, segment_index -> track_id) contra la identidad
verdadera generada por make_dense_case.py (`<base>_truth.csv`).

Como ambos archivos indexan exactamente los mismos segmentos, no hace falta emparejar
detecciones: el problema es puro de identidad, y las metricas estandar de seguimiento
multiobjeto se reducen a comparar dos particiones del mismo conjunto de observaciones.

  IDF1     F1 de identidad: se busca la correspondencia optima uno-a-uno entre tracks
           verdaderos y predichos (asignacion hungara sobre la matriz de coincidencias)
           y se mide que fraccion de las observaciones queda bien identificada. 1 = perfecto.
  IDSW     cambios de identidad: veces que un track verdadero pasa de un track predicho
           a otro a lo largo de su vida. 0 = perfecto.
  MOTA     1 - (no asignadas + IDSW) / observaciones. 1 = perfecto.
  FRAG     numero medio de tracks predichos por track verdadero. 1 = perfecto.

Uso:
    python3 evaluate_mot.py dense_n100_truth.csv salida_assignments.csv \
        --truth-tracks dense_n100_truth_tracks.csv --pred-tracks salida_tracks.csv
"""
import argparse
import csv
from collections import Counter, defaultdict

import numpy as np
from scipy.optimize import linear_sum_assignment


def read_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def load_pairs(truth_path, pred_path):
    """Devuelve la lista de (frame, true_id, pred_id) unida por (frame, segment_index)."""
    pred = {}
    for r in read_csv(pred_path):
        pred[(int(r["source_frame"]), int(r["segment_index"]))] = int(r["track_id"])

    pairs, missing = [], 0
    for r in read_csv(truth_path):
        key = (int(r["frame"]), int(r["segment_index"]))
        if key not in pred:
            missing += 1
            continue
        pairs.append((key[0], int(r["true_track_id"]), pred[key]))
    return pairs, missing


def evaluate(pairs):
    total = len(pairs)
    unassigned = sum(1 for _, _, p in pairs if p < 0)

    # --- IDF1 por asignacion optima entre identidades verdaderas y predichas -------
    true_ids = sorted({t for _, t, _ in pairs})
    pred_ids = sorted({p for _, _, p in pairs if p >= 0})
    ti = {t: i for i, t in enumerate(true_ids)}
    pi = {p: i for i, p in enumerate(pred_ids)}

    counts = np.zeros((len(true_ids), len(pred_ids)), dtype=np.int64)
    for _, t, p in pairs:
        if p >= 0:
            counts[ti[t], pi[p]] += 1

    rows, cols = linear_sum_assignment(-counts)
    idtp = int(counts[rows, cols].sum())
    idfn = total - idtp
    idfp = total - unassigned - idtp
    idf1 = 2 * idtp / (2 * idtp + idfp + idfn) if total else 0.0

    # --- Cambios de identidad y fragmentacion --------------------------------------
    by_true = defaultdict(list)
    for frame, t, p in pairs:
        by_true[t].append((frame, p))
    idsw = 0
    frags = []
    for t, seq in by_true.items():
        seq.sort()
        seen = [p for _, p in seq if p >= 0]
        frags.append(len(set(seen)) if seen else 0)
        for a, b in zip(seen, seen[1:]):
            if a != b:
                idsw += 1

    mota = 1.0 - (unassigned + idsw) / total if total else 0.0
    return {
        "observaciones": total,
        "tracks_verdaderos": len(true_ids),
        "tracks_predichos": len(pred_ids),
        "IDF1": idf1,
        "IDSW": idsw,
        "MOTA": mota,
        "FRAG": float(np.mean(frags)) if frags else 0.0,
        "no_asignadas": unassigned,
    }


def velocity_error(pairs, truth_tracks_path, pred_tracks_path):
    """Error relativo de velocidad, comparando cada track verdadero con su predicho dominante."""
    truth_v = {int(r["true_track_id"]): float(r["speed"]) for r in read_csv(truth_tracks_path)}
    pred_v = {}
    for r in read_csv(pred_tracks_path):
        if r["velocity"].strip():
            pred_v[int(r["track_id"])] = float(r["velocity"])

    dominant = defaultdict(Counter)
    for _, t, p in pairs:
        if p >= 0:
            dominant[t][p] += 1

    errors, matched = [], 0
    for t, counter in dominant.items():
        p = counter.most_common(1)[0][0]
        if p in pred_v and truth_v.get(t):
            matched += 1
            errors.append(abs(pred_v[p] - truth_v[t]) / truth_v[t])
    if not errors:
        return None
    errors = np.array(errors)
    return {
        "tracks_con_velocidad": matched,
        "error_mediano_%": float(np.median(errors) * 100),
        "error_medio_%": float(errors.mean() * 100),
        "error_p90_%": float(np.percentile(errors, 90) * 100),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("truth")
    ap.add_argument("assignments")
    ap.add_argument("--truth-tracks")
    ap.add_argument("--pred-tracks")
    ap.add_argument("--label", default="")
    args = ap.parse_args()

    pairs, missing = load_pairs(args.truth, args.assignments)
    if missing:
        print(f"AVISO: {missing} segmentos de la verdad de terreno no aparecen en las asignaciones")

    result = evaluate(pairs)
    if args.label:
        print(f"== {args.label}")
    for k, v in result.items():
        print(f"  {k:20s} {v:.4f}" if isinstance(v, float) else f"  {k:20s} {v}")

    if args.truth_tracks and args.pred_tracks:
        verr = velocity_error(pairs, args.truth_tracks, args.pred_tracks)
        if verr:
            for k, v in verr.items():
                print(f"  {k:20s} {v:.4f}" if isinstance(v, float) else f"  {k:20s} {v}")


if __name__ == "__main__":
    main()
