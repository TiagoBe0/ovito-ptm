#!/usr/bin/env python3
"""
ml_prototype_rearrangement.py
=============================
Prototipo: predecir QUE ATOMOS cambian de hibridacion en el proximo ciclo mCRA
a partir del entorno local del ciclo actual.

Motivacion
----------
Los dos usos "obvios" de ML sobre carbono amorfo dan negativo en esta muestra
(medido con probe_ambiguity / probe_geometry, ver README):

  * Resolver la ambiguedad del cutoff de coordinacion: dentro de la meseta
    1.98-2.14 solo 0.1-0.4% de los atomos cambian de etiqueta. El problema ya
    quedo resuelto al igualar bond_cutoff al coordination_cutoff (2.064).
  * Corregir la regla CN->hibridacion con geometria: la geometria CONFIRMA la
    regla (CN=4 da angulo medio 109.3 grados contra 109.47 ideal, lambda1=0.27;
    CN=3 da 117.7 grados y lambda1=0.02). Distribuciones unimodales, sin
    poblacion mal etiquetada que rescatar.

Lo que si es dificil y no es circular: el reordenamiento es un evento FUTURO.
La etiqueta sale del ciclo N+1 y los descriptores del ciclo N, asi que ninguna
regla del ciclo N puede "hacer trampa". Es el planteo de softness en vidrios
(Cubuk et al. PRL 114, 108001 (2015); Schoenholz et al. Nat. Phys. 12, 469
(2016)), que encaja con este dataset porque los cycleN.data estan minimizados a
T=0: el churn es reordenamiento estructural real, no ruido termico.

Etiqueta
--------
y_i = 1 si la hibridacion (regla CN: 2->sp, 3->sp2, 4->sp3, resto->other) del
atomo i cambia entre el ciclo N y el N+1. Tasa base ~2-2.5%.

Descriptores (ciclo N solamente)
--------------------------------
Todos son continuos y casi todos libres de cutoff (k vecinos mas cercanos), que
es justamente la informacion que la regla binaria descarta.

CUIDADO: los cycleN.data NO traen los atomos en el mismo orden entre ciclos.
Hay que ordenar por atom_id antes de comparar ciclos, o se comparan atomos
distintos (da ~47% de cambio en vez de 2%).

Uso
---
    python research/amorphous_carbon/scripts/ml_prototype_rearrangement.py \
        --train 50 --test 0,99 --output results/ml_rearrangement.csv
"""

import argparse
import sys
import time
from pathlib import Path

import numpy as np
from scipy.spatial import cKDTree

sys.path.insert(0, "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/src")
from ac_analyzer.core.reader import parse_lammps_data  # noqa: E402

DATASET = ("/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-"
           "carbon-ku8jju/samples_for_analysis/camorfo_sp3_60_mCRA")

COORD_CUTOFF = 2.064   # igual al default del AmorphousCarbonModifier
KMAX = 8               # vecinos usados para los descriptores k-NN
SOFT_WIDTHS = (0.05, 0.15)


# ===========================================================================
# Carga y descriptores
# ===========================================================================

def load_sorted(cycle, dataset):
    """Devuelve posiciones envueltas y ordenadas por atom_id (imprescindible)."""
    frame = parse_lammps_data(f"{dataset}/cycle{cycle}.data")
    order = np.argsort(np.asarray(frame.atom_id))
    pos = frame.positions[order]
    box = frame.box_lengths
    return pos - np.floor(pos / box) * box, box


def hybridization(cn):
    """Regla del AmorphousCarbonModifier: 0 other, 1 sp, 2 sp2, 3 sp3."""
    h = np.zeros_like(cn)
    h[cn == 2] = 1
    h[cn == 3] = 2
    h[cn == 4] = 3
    return h


def coordination_only(w, box):
    """Coordinacion sola — para construir la etiqueta del ciclo N+1."""
    tree = cKDTree(w, boxsize=box)
    dist, _ = tree.query(w, k=KMAX + 1)
    return (dist[:, 1:] <= COORD_CUTOFF).sum(1)


def descriptors(w, box):
    """Bloque de descriptores del entorno local. Devuelve (X, nombres, cn)."""
    tree = cKDTree(w, boxsize=box)
    dist, idx = tree.query(w, k=KMAX + 1)
    dist, idx = dist[:, 1:], idx[:, 1:]
    mask = dist <= COORD_CUTOFF
    cn = mask.sum(1)

    # Versores a los vecinos (minimum image), anulando los de fuera del cutoff.
    vec = (w[idx] - w[:, None, :] + box / 2) % box - box / 2
    u = vec / np.maximum(np.linalg.norm(vec, axis=2, keepdims=True), 1e-12)
    u_in = u * mask[:, :, None]

    # Tensor de forma de la nube de versores: lambda1 ~ 0 plano, ~1/3 tetraedrico.
    shape = np.einsum('nki,nkj->nij', u_in, u_in) / np.maximum(cn, 1)[:, None, None]
    ev = np.linalg.eigvalsh(shape)
    lam1, lam2 = ev[:, 0], ev[:, 1]

    # Angulos entre pares de vecinos dentro del cutoff.
    cosines = np.einsum('nki,nli->nkl', u_in, u_in)
    pair = mask[:, :, None] & mask[:, None, :]
    iu = np.triu_indices(KMAX, k=1)
    cvals, pvals = cosines[:, iu[0], iu[1]], pair[:, iu[0], iu[1]]
    npair = np.maximum(pvals.sum(1), 1)
    ang = np.degrees(np.arccos(np.clip(cvals, -1, 1)))
    ang_mean = (ang * pvals).sum(1) / npair
    ang_var = (((ang - ang_mean[:, None]) ** 2) * pvals).sum(1) / npair
    ang_std = np.sqrt(ang_var)

    # Coordinacion suave: version continua del conteo duro, sin umbral.
    soft = [1.0 / (1.0 + np.exp((dist - COORD_CUTOFF) / wdt)) for wdt in SOFT_WIDTHS]
    soft = [s.sum(1) for s in soft]

    # "Holgura" del enlace marginal: cuanto sobra/falta para el proximo cambio
    # de coordinacion. Es la distancia al borde de la decision, en Angstrom.
    d_sorted = dist
    slack_in = COORD_CUTOFF - np.take_along_axis(
        d_sorted, np.maximum(cn - 1, 0)[:, None], axis=1).ravel()   # ultimo enlace contado
    slack_out = np.take_along_axis(
        d_sorted, np.minimum(cn, KMAX - 1)[:, None], axis=1).ravel() - COORD_CUTOFF  # primero excluido

    base = {
        "cn": cn.astype(np.float64),
        "lam1": lam1, "lam2": lam2,
        "ang_mean": ang_mean, "ang_std": ang_std,
        f"soft_cn_{SOFT_WIDTHS[0]}": soft[0],
        f"soft_cn_{SOFT_WIDTHS[1]}": soft[1],
        "slack_in": slack_in, "slack_out": slack_out,
    }
    for k in range(KMAX):
        base[f"d{k+1}"] = dist[:, k]

    # Coarse-graining: promedio sobre los vecinos. En la literatura de softness
    # el contexto del entorno agrega tanta informacion como el sitio mismo.
    for name in ("lam1", "cn", "slack_in", "d4"):
        base[f"cg_{name}"] = base[name][idx].mean(1)

    names = list(base.keys())
    X = np.column_stack([base[n] for n in names])
    return X, names, cn


# ===========================================================================
# Metricas
# ===========================================================================

def top_budget_precision(y, score, budget):
    """Precision entre los `budget` (fraccion) atomos de score mas alto."""
    k = max(int(round(budget * len(y))), 1)
    top = np.argpartition(-score, k - 1)[:k]
    return y[top].mean()


def evaluate(name, y, score, base_rate, rows, tag):
    from sklearn.metrics import roc_auc_score, average_precision_score
    auc = roc_auc_score(y, score)
    ap = average_precision_score(y, score)
    prec = top_budget_precision(y, score, base_rate)
    print(f"    {name:<22} AUC={auc:.4f}  AP={ap:.4f}  "
          f"P@{100*base_rate:.1f}%={prec:.4f}  enriq={prec/base_rate:.2f}x")
    rows.append(dict(case=tag, model=name, auc=auc, ap=ap,
                     precision_at_budget=prec, enrichment=prec / base_rate,
                     base_rate=base_rate))
    return auc


# ===========================================================================
# Main
# ===========================================================================

def build_case(cycle, dataset):
    """Descriptores del ciclo N y etiqueta de cambio hacia el N+1."""
    w_now, box = load_sorted(cycle, dataset)
    w_next, _ = load_sorted(cycle + 1, dataset)
    X, names, cn_now = descriptors(w_now, box)
    cn_next = coordination_only(w_next, box)
    y = (hybridization(cn_now) != hybridization(cn_next)).astype(np.int8)
    return X, y, names


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dataset", default=DATASET)
    ap.add_argument("--train", default="50", help="ciclo N de entrenamiento (usa N->N+1)")
    ap.add_argument("--test", default="0,99", help="ciclos N de test, separados por coma")
    ap.add_argument("--output", default=None, help="CSV de metricas")
    args = ap.parse_args()

    train_cycle = int(args.train)
    test_cycles = [int(c) for c in args.test.split(",")]

    from sklearn.ensemble import HistGradientBoostingClassifier
    from sklearn.linear_model import LogisticRegression
    from sklearn.preprocessing import StandardScaler
    from sklearn.inspection import permutation_importance

    t0 = time.time()
    print(f"[train] ciclo {train_cycle} -> {train_cycle+1}")
    Xtr, ytr, names = build_case(train_cycle, args.dataset)
    print(f"    N={len(ytr)}  positivos={ytr.sum()} ({100*ytr.mean():.2f}%)  "
          f"features={len(names)}  ({time.time()-t0:.1f}s)")

    gbdt = HistGradientBoostingClassifier(max_iter=300, random_state=0)
    gbdt.fit(Xtr, ytr)

    scaler = StandardScaler().fit(Xtr)
    logit = LogisticRegression(max_iter=2000).fit(scaler.transform(Xtr), ytr)
    print(f"    entrenado ({time.time()-t0:.1f}s)")

    rows = []
    for cyc in test_cycles:
        print(f"\n[test] ciclo {cyc} -> {cyc+1}  (held-out: ciclo distinto al de entrenamiento)")
        Xte, yte, _ = build_case(cyc, args.dataset)
        base = yte.mean()
        print(f"    N={len(yte)}  positivos={yte.sum()} (tasa base {100*base:.2f}%)")
        tag = f"c{cyc}->{cyc+1}"

        # Baselines de una sola variable: lo que ya se puede leer del dump.
        i_d4 = names.index("d4")
        i_lam1 = names.index("lam1")
        i_slack = names.index("slack_in")
        evaluate("baseline d4", yte, Xte[:, i_d4], base, rows, tag)
        evaluate("baseline -lam1", yte, -Xte[:, i_lam1], base, rows, tag)
        evaluate("baseline -slack_in", yte, -Xte[:, i_slack], base, rows, tag)
        evaluate("logistica", yte, logit.predict_proba(scaler.transform(Xte))[:, 1],
                 base, rows, tag)
        evaluate("GBDT", yte, gbdt.predict_proba(Xte)[:, 1], base, rows, tag)

    # -------------------------------------------------------------------
    # Control de trivialidad. Un atomo cuyo enlace marginal ya esta pegado al
    # cutoff cambia de coordinacion con cualquier temblor: predecirlo no es
    # fisica, es leer el borde de la decision. Se repite la evaluacion sobre
    # subpoblaciones cada vez mas lejos de ese borde. Si la ventaja del modelo
    # sobrevive ahi, la senal es estructural.
    # -------------------------------------------------------------------
    print("\n[control de trivialidad] excluyendo atomos cerca del borde de decision")
    i_in, i_out = names.index("slack_in"), names.index("slack_out")
    for cyc in test_cycles:
        Xte, yte, _ = build_case(cyc, args.dataset)
        score = gbdt.predict_proba(Xte)[:, 1]
        margin = np.minimum(Xte[:, i_in], Xte[:, i_out])
        print(f"  ciclo {cyc}->{cyc+1}:")
        for thr in (0.0, 0.05, 0.10, 0.20):
            keep = margin >= thr
            y, s, sl = yte[keep], score[keep], -Xte[keep, i_in]
            if y.sum() < 50:
                continue
            b = y.mean()
            e_ml = top_budget_precision(y, s, b) / b
            e_bl = top_budget_precision(y, sl, b) / b
            print(f"    margen>={thr:.2f} A  n={keep.sum():>7} ({100*keep.mean():>5.1f}%)  "
                  f"base={100*b:>5.2f}%  GBDT {e_ml:>5.2f}x  baseline slack {e_bl:>5.2f}x")
            rows.append(dict(case=f"c{cyc}->{cyc+1}", model=f"GBDT margen>={thr:.2f}",
                             auc=float("nan"), ap=float("nan"),
                             precision_at_budget=top_budget_precision(y, s, b),
                             enrichment=e_ml, base_rate=b))

    # Importancia por permutacion sobre una submuestra del ultimo test.
    print("\n[importancia por permutacion] (submuestra 40k del ultimo test)")
    sub = np.random.default_rng(0).choice(len(yte), size=min(40000, len(yte)), replace=False)
    imp = permutation_importance(gbdt, Xte[sub], yte[sub], n_repeats=3,
                                 random_state=0, scoring="roc_auc")
    order = np.argsort(-imp.importances_mean)
    for i in order[:10]:
        print(f"    {names[i]:<18} {imp.importances_mean[i]:+.4f} +- {imp.importances_std[i]:.4f}")

    if args.output:
        import csv
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        with out.open("w", newline="") as fh:
            wr = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
            wr.writeheader()
            wr.writerows(rows)
        print(f"\nmetricas -> {out}")
    print(f"total {time.time()-t0:.1f}s")


if __name__ == "__main__":
    main()
