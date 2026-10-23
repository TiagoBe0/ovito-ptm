#!/usr/bin/env python3
"""Valida la velocidad por dislocacion del tracker C++ contra la relacion de Orowan.

Lee los dos CSV que escribe el runner headless

    build/bin/ovito --nogui --dislocation-track \
        --dislocation-track-input  dump.shear_au.lammpstrj \
        --dislocation-track-output metrics.csv

es decir metrics.csv (una fila por frame) y metrics_tracks.csv (una fila por
dislocacion, con su velocidad de drift), y compara la velocidad medida por track
contra v_orowan = gammadot / (rho * b).

Uso:
    python3 analyze_calib_edge.py metrics.csv --erate 8.6e-4 --a0 4.078
"""
import argparse
import csv
import math
import statistics


# Velocidad: el tracker trabaja en A/ps (longitud de la celda / time-per-frame).
A_PS_TO_M_S = 100.0


def read_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def to_float(value):
    """Convierte un campo del CSV a float; devuelve None si esta vacio."""
    if value is None or value.strip() == "":
        return None
    return float(value)


def burgers_length(family, a0):
    """|b| en Angstrom para la familia de vector de Burgers que reporta DXA."""
    if not family:
        return None
    f = family.lower()
    if "1/2" in f and "110" in f:      # perfecta
        return a0 / math.sqrt(2.0)
    if "1/6" in f and "112" in f:      # parcial de Shockley
        return a0 / math.sqrt(6.0)
    if "1/6" in f and "110" in f:      # stair-rod
        return a0 / math.sqrt(18.0)
    if "1/3" in f and "100" in f:      # Hirth
        return a0 / 3.0
    if "1/3" in f and "111" in f:      # Frank
        return a0 / math.sqrt(3.0)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("metrics", help="CSV por frame; el de tracks se deduce (<base>_tracks.csv)")
    ap.add_argument("--tracks", default=None, help="CSV por track, si no sigue la convencion")
    ap.add_argument("--erate", type=float, required=True, help="tasa de corte aplicada, 1/ps")
    ap.add_argument("--a0", type=float, default=4.078, help="parametro de red, A")
    args = ap.parse_args()

    tracks_path = args.tracks
    if tracks_path is None:
        base = args.metrics[:-4] if args.metrics.endswith(".csv") else args.metrics
        tracks_path = base + "_tracks.csv"

    frames = read_csv(args.metrics)
    tracks = read_csv(tracks_path)

    # --- Velocidad medida, una por dislocacion -------------------------------
    measured = [(t, to_float(t["velocity"])) for t in tracks]
    measured = [(t, v) for t, v in measured if v is not None]
    print(f"tracks totales           : {len(tracks)}")
    print(f"tracks con velocidad     : {len(measured)}")
    if not measured:
        print("Sin velocidades medidas: los tracks son demasiado cortos "
              "(el ajuste de drift exige un span minimo de max(5, nframes/20) frames).")
        return

    speeds = sorted(v * A_PS_TO_M_S for _, v in measured)
    v_med = statistics.median(speeds)
    print(f"v_tracker mediana        : {v_med:8.1f} m/s")
    print(f"v_tracker media          : {statistics.fmean(speeds):8.1f} m/s")
    print(f"v_tracker rango          : {speeds[0]:8.1f} .. {speeds[-1]:.1f} m/s")

    # --- Referencia de Orowan ------------------------------------------------
    # v = gammadot / (rho b), con rho promediada sobre los frames que tienen linea.
    densities = [d for d in (to_float(f["total_density"]) for f in frames) if d]
    if not densities:
        print("El CSV por frame no tiene densidad > 0: no hay referencia de Orowan.")
        return
    rho = statistics.fmean(densities)   # A^-2

    families = [t["burgers_family"] for t, _ in measured]
    b_values = [b for b in (burgers_length(f, args.a0) for f in families) if b]
    if not b_values:
        print(f"Familias de Burgers no reconocidas: {sorted(set(families))}")
        return
    b = statistics.fmean(b_values)      # A

    v_orowan = args.erate / (rho * b) * A_PS_TO_M_S
    print(f"rho media                : {rho:.3e} A^-2  ({rho * 1e20:.2e} m^-2)")
    print(f"b (familia dominante)    : {b:.4f} A  [{max(set(families), key=families.count)}]")
    print(f"v_orowan                 : {v_orowan:8.1f} m/s")
    print(f"ratio v_tracker/v_orowan : {v_med / v_orowan:8.2f}")


if __name__ == "__main__":
    main()
