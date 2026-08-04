#!/usr/bin/env python3
"""Compara los CSV del barrido de sensibilidad de cutoff contra el baseline.

Responde: la caida de retained_fraction sobrevive a variar los cutoffs (~+-5%),
o es un artefacto del umbral CN=3/CN=4?

Uso:
    python3 compare_cutoff_sweep.py
"""
import csv
from pathlib import Path

BASE = Path(__file__).resolve().parent.parent / "results"
BASELINE = BASE / "camorfo_sp3_60_mCRA_sp3_track.csv"
SWEEP = BASE / "cutoff_sweep"

VARIANTS = [
    ("baseline   coord 2.064 bond 1.85", BASELINE),
    ("coord 1.95  (-5%)  bond 1.85", SWEEP / "coord195_bond185.csv"),
    ("coord 2.15  (+4%)  bond 1.85", SWEEP / "coord215_bond185.csv"),
    ("coord 2.064 bond 1.75 (-5%)", SWEEP / "coord2064_bond175.csv"),
    ("coord 2.064 bond 1.95 (+5%)", SWEEP / "coord2064_bond195.csv"),
]


def load(path):
    if not path.exists():
        return None
    with open(path) as fh:
        return {int(r["source_frame"]): r for r in csv.DictReader(fh)}


def main():
    tables = [(name, load(p)) for name, p in VARIANTS]
    tables = [(n, t) for n, t in tables if t]

    def show(title, fn, fmt="{:>10}"):
        print(f"\n{title}")
        print(f"{'variante':<32} " + " ".join(f"{'c'+str(c):>10}" for c in (0, 1, 10, 50, 100)))
        for name, t in tables:
            cells = []
            for c in (0, 1, 10, 50, 100):
                cells.append(fmt.format(fn(t[c])) if c in t else fmt.format("-"))
            print(f"{name:<32} " + " ".join(cells))

    show("sp3_percent (composicion global)",
         lambda r: f"{float(r['sp3_percent']):.2f}")
    show("largest_cluster_fraction (que tan intacta esta la matriz)",
         lambda r: f"{float(r['largest_cluster_fraction']):.4f}")
    show("retained_fraction (del cluster de referencia rango 1)",
         lambda r: f"{float(r['retained_fraction']):.4f}")
    show("left_phase_fraction (atomos que dejaron CN=4)",
         lambda r: f"{float(r['left_phase_fraction']):.4f}")
    show("fragment_count",
         lambda r: f"{int(r['fragment_count'])}")

    print("\n\nCaida relativa de retained_fraction entre ciclo 0 y 100:")
    for name, t in tables:
        if 100 in t:
            drop = 1.0 - float(t[100]["retained_fraction"])
            print(f"  {name:<32} {drop*100:6.2f}%")


if __name__ == "__main__":
    main()
