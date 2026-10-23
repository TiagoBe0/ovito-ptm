#!/usr/bin/env python3
"""Velocidad de referencia por dislocacion, independiente del tracker C++.

Corre el DXA del modulo oficial de ovito sobre la trayectoria del caso controlado
y sigue cada parcial por su vector de Burgers espacial (los cuatro segmentos del
dipolo disociado tienen combinaciones de signo (bx, bz) unicas). Desenrolla la
coordenada de glide x y ajusta su pendiente por minimos cuadrados: esa pendiente
es la velocidad de deriva, la misma cantidad que reporta el tracker.

Uso:
    python3 ground_truth_velocity.py <dump> --dt 0.1
"""
import argparse

import numpy as np
from ovito.io import import_file
from ovito.modifiers import DislocationAnalysisModifier


A_PS_TO_M_S = 100.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--dt", type=float, default=0.1, help="tiempo entre frames, ps")
    args = ap.parse_args()

    pipe = import_file(args.dump)
    pipe.modifiers.append(DislocationAnalysisModifier(
        input_crystal_structure=DislocationAnalysisModifier.Lattice.FCC))

    tracks = {}     # (signo bx, signo bz) -> lista de (frame, x del centroide)
    for f in range(pipe.source.num_frames):
        data = pipe.compute(f)
        lx = data.cell[0, 0]
        for seg in data.dislocations.segments:
            b = np.asarray(seg.spatial_burgers_vector)
            key = (int(np.sign(round(b[0], 3))), int(np.sign(round(b[2], 3))))
            x = np.asarray(seg.points).mean(axis=0)[0]
            hist = tracks.setdefault(key, [])
            if hist:                      # desenrollar x a traves de la frontera periodica
                x -= lx * round((x - hist[-1][1]) / lx)
            hist.append((f, x))

    print(f"{'b (bx,bz)':>12} {'frames':>7} {'v_x [A/ps]':>12} {'v_x [m/s]':>11}")
    speeds = []
    for key, hist in sorted(tracks.items()):
        t = np.array([f for f, _ in hist], dtype=float) * args.dt
        x = np.array([x for _, x in hist])
        slope = np.polyfit(t, x, 1)[0]
        speeds.append(abs(slope))
        print(f"{str(key):>12} {len(hist):>7} {slope:>12.4f} {abs(slope) * A_PS_TO_M_S:>11.1f}")

    speeds = np.array(speeds)
    print(f"\nmediana |v| : {np.median(speeds) * A_PS_TO_M_S:.1f} m/s")
    print(f"media   |v| : {speeds.mean() * A_PS_TO_M_S:.1f} m/s")


if __name__ == "__main__":
    main()
