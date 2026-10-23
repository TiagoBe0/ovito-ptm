#!/usr/bin/env python3
"""Genera redes de dislocaciones sinteticas densas con identidad conocida (formato .ca).

El tracker C++ toma como entrada un DislocationNetwork, que es exactamente lo que
serializa el formato .ca de OVITO. Escribiendo directamente esos archivos se puede
alimentar al binario con una poblacion densa de lineas cuya identidad, velocidad y
eventos se conocen frame a frame, sin pasar por atomos ni por DXA. Eso separa lo que
se quiere medir (el algoritmo de seguimiento) de lo que no (la deteccion por DXA).

La trayectoria reproduce a proposito los dos artefactos que rompen el matching ingenuo:

  - origen de desenrollado aleatorio: DXA entrega una linea que cierra por la frontera
    periodica arrancando en un punto arbitrario del bucle, asi que su centroide se corre
    a lo largo de la propia linea entre frames;
  - reorientacion de cluster: cada tanto DXA re-elige el marco de referencia del cristal
    y todos los vectores de Burgers cristalinos saltan a la vez a una variante equivalente.

Salidas: <base>.ca (N frames concatenados) y <base>_truth.csv con la identidad verdadera
de cada segmento de cada frame, mas <base>_truth_tracks.csv con la velocidad verdadera.

Uso:
    python3 make_dense_case.py --tracks 100 --frames 60 --out dense_n100
"""
import argparse
import csv
import itertools

import numpy as np


# Familias de Burgers de FCC en unidades de red, tal como las reporta DXA.
FAMILIES = [
    ("1/2<110> (Perfect)", np.array([0.5, 0.5, 0.0]), (0.2, 0.2, 1.0)),
    ("1/6<112> (Shockley)", np.array([1 / 6, 1 / 6, 2 / 6]), (0.0, 1.0, 0.0)),
    ("1/6<110> (Stair-rod)", np.array([1 / 6, 1 / 6, 0.0]), (1.0, 0.0, 1.0)),
    ("1/3<100> (Hirth)", np.array([1 / 3, 0.0, 0.0]), (1.0, 1.0, 0.0)),
    ("1/3<111> (Frank)", np.array([1 / 3, 1 / 3, 1 / 3]), (0.0, 1.0, 1.0)),
]

# Rotaciones propias del grupo cubico: permutaciones de ejes con determinante +1.
CUBIC_ROTATIONS = [
    m for m in (
        np.array([[1 if p[i] == j else 0 for j in range(3)] for i in range(3)]) * np.array(s)
        for p in itertools.permutations(range(3))
        for s in itertools.product([1, -1], repeat=3)
    ) if abs(np.linalg.det(m) - 1.0) < 1e-9
]


def random_perpendicular(v, rng):
    """Devuelve un unitario perpendicular a v."""
    while True:
        w = rng.normal(size=3)
        w -= np.dot(w, v) * v
        n = np.linalg.norm(w)
        if n > 1e-6:
            return w / n


class Track:
    """Una dislocacion sintetica: linea recta que se traslada a velocidad constante.

    Dos poblaciones, como en un cristal real:
      - `pbc_closed`: la linea atraviesa la celda y se cierra sobre si misma por la frontera
        periodica (extremos coincidentes modulo PBC). Su posicion a lo largo de la propia linea
        NO es observable, y DXA la desenrolla desde un punto arbitrario del bucle.
      - abiertas: segmento finito con dos extremos reales, cuya posicion a lo largo de la linea
        si es observable.
    """

    def __init__(self, tid, rng, args):
        self.tid = tid
        self.family = rng.integers(0, 2)                      # perfectas y Shockley
        name, b, _ = FAMILIES[self.family]
        # Variante aleatoria de la familia (signo y permutacion), en unidades de red.
        self.burgers = CUBIC_ROTATIONS[rng.integers(len(CUBIC_ROTATIONS))] @ b
        self.family_name = name

        self.pbc_closed = rng.random() < args.pbc_fraction
        if self.pbc_closed:
            # Recta a lo largo de un eje de la celda: cruza la caja y se cierra por PBC.
            axis = int(rng.integers(3))
            self.line_dir = np.eye(3)[axis]
            self.length = args.box
        else:
            self.line_dir = rng.normal(size=3)
            self.line_dir /= np.linalg.norm(self.line_dir)
            self.length = rng.uniform(*args.length_range)
        self.origin = rng.uniform(0, args.box, size=3)
        # La velocidad es de glide: perpendicular a la linea.
        speed = rng.lognormal(np.log(args.speed), args.speed_spread)
        self.velocity = speed * random_perpendicular(self.line_dir, rng)

        # Vida del track: nace y muere dentro del trayecto con probabilidad controlada.
        self.birth, self.death = 0, args.frames - 1
        if rng.random() < args.event_rate:
            self.birth = int(rng.integers(1, max(2, args.frames // 2)))
        if rng.random() < args.event_rate:
            self.death = int(rng.integers(max(self.birth + args.min_life, 2), args.frames))
            self.death = min(self.death, args.frames - 1)

    def alive(self, frame):
        return self.birth <= frame <= self.death

    def centroid(self, frame, box):
        return np.mod(self.origin + self.velocity * frame, box)

    def polyline(self, frame, rng, args):
        """Puntos de la linea en este frame, con ruido y origen de desenrollado aleatorio."""
        c = self.centroid(frame, args.box)
        n = max(2, int(self.length / args.point_spacing))
        s = np.linspace(-self.length / 2, self.length / 2, n)
        if args.unwrap_jitter and self.pbc_closed:
            # Solo las lineas cerradas por PBC tienen origen de desenrollado arbitrario: DXA abre
            # el bucle en cualquier punto. Una linea abierta tiene extremos reales y no se corre.
            s = s + rng.uniform(-self.length / 2, self.length / 2)
        pts = c + np.outer(s, self.line_dir)
        if args.noise > 0:
            pts = pts + rng.normal(0.0, args.noise, size=pts.shape)
        return pts


def write_frame(f, frame_no, segments, args, rotation):
    """Escribe un bloque .ca completo (un frame)."""
    f.write("CA_FILE_VERSION 6\nCA_LIB_VERSION 0.0.0\n")
    f.write("STRUCTURE_TYPES 1\nSTRUCTURE_TYPE 1\nNAME fcc\nFULL_NAME Face-centered cubic\n")
    f.write("COLOR 0.4 1 0.4\nTYPE LATTICE\n")
    f.write(f"BURGERS_VECTOR_FAMILIES {len(FAMILIES)}\n")
    for i, (name, b, color) in enumerate(FAMILIES):
        f.write(f"BURGERS_VECTOR_FAMILY ID {i}\n{name}\n")
        f.write(f"{b[0]:.8f} {b[1]:.8f} {b[2]:.8f}\n")
        f.write(f"{color[0]} {color[1]} {color[2]}\n")
    f.write("END_STRUCTURE_TYPE\n")

    f.write("SIMULATION_CELL_ORIGIN 0 0 0\nSIMULATION_CELL_MATRIX\n")
    for row in range(3):
        f.write(" ".join(f"{args.box if row == c else 0:.8f}" for c in range(3)) + "\n")
    f.write("PBC_FLAGS 1 1 1\n")

    # Un unico cluster; su orientacion lleva el parametro de red, de modo que el vector de
    # Burgers espacial queda en Angstrom y el cristalino en unidades de red, como en DXA real.
    f.write("CLUSTERS 1\nCLUSTER 1\nCLUSTER_STRUCTURE 1\nCLUSTER_ORIENTATION\n")
    for row in range(3):
        f.write(" ".join(f"{args.a0 * rotation[row][c]:.8f}" for c in range(3)) + "\n")
    f.write("CLUSTER_COLOR 0.6 0.6 0.6\nCLUSTER_SIZE 100000\nEND_CLUSTER\n")
    f.write("CLUSTER_TRANSITIONS 0\n")

    f.write(f"DISLOCATIONS {len(segments)}\n")
    for idx, (burgers, pts) in enumerate(segments):
        f.write(f"{idx}\n")
        f.write(f"{burgers[0]:.8f} {burgers[1]:.8f} {burgers[2]:.8f}\n")
        f.write("1\n")
        f.write(f"{len(pts)}\n")
        for p in pts:
            f.write(f"{p[0]:.6f} {p[1]:.6f} {p[2]:.6f} 1\n")
    # Todos los extremos quedan sueltos (sin uniones): cada nodo apunta a si mismo.
    f.write("DISLOCATION_JUNCTIONS\n")
    for idx in range(len(segments)):
        f.write(f"1 {idx}\n0 {idx}\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tracks", type=int, default=100, help="numero de dislocaciones")
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--box", type=float, default=400.0, help="arista de la celda, A")
    ap.add_argument("--a0", type=float, default=4.078)
    ap.add_argument("--speed", type=float, default=2.0, help="mediana de |v|, A/frame")
    ap.add_argument("--speed-spread", type=float, default=0.5, help="sigma del lognormal de |v|")
    ap.add_argument("--length-range", type=float, nargs=2, default=[30.0, 120.0])
    ap.add_argument("--pbc-fraction", type=float, default=0.5,
                    help="fraccion de lineas que cruzan la celda y cierran por PBC")
    ap.add_argument("--point-spacing", type=float, default=6.0)
    ap.add_argument("--noise", type=float, default=0.5, help="sigma del ruido en los puntos, A")
    ap.add_argument("--dropout", type=float, default=0.0, help="prob. de que DXA pierda un segmento")
    ap.add_argument("--event-rate", type=float, default=0.0, help="prob. de nacimiento/muerte por track")
    ap.add_argument("--min-life", type=int, default=10)
    ap.add_argument("--unwrap-jitter", action="store_true", default=True)
    ap.add_argument("--no-unwrap-jitter", dest="unwrap_jitter", action="store_false")
    ap.add_argument("--variant-jitter", type=float, default=0.1,
                    help="prob. por frame de que el cluster se reoriente (todos los b cristalinos saltan)")
    ap.add_argument("--seed", type=int, default=12345)
    ap.add_argument("--out", default="dense")
    args = ap.parse_args()

    rng = np.random.default_rng(args.seed)
    tracks = [Track(i, rng, args) for i in range(args.tracks)]

    truth_rows = []
    rotation = np.eye(3)
    with open(args.out + ".ca", "w") as f:
        for frame in range(args.frames):
            if frame > 0 and rng.random() < args.variant_jitter:
                rotation = CUBIC_ROTATIONS[rng.integers(len(CUBIC_ROTATIONS))]
            present = [t for t in tracks if t.alive(frame) and rng.random() >= args.dropout]
            rng.shuffle(present)              # el orden no debe llevar informacion de identidad
            segments = []
            for t in present:
                # El vector cristalino se expresa en el marco vigente del cluster.
                segments.append((rotation.T @ t.burgers, t.polyline(frame, rng, args)))
            write_frame(f, frame, segments, args, rotation)
            for idx, t in enumerate(present):
                truth_rows.append((frame, idx, t.tid))

    with open(args.out + "_truth.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["frame", "segment_index", "true_track_id"])
        w.writerows(truth_rows)

    with open(args.out + "_truth_tracks.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["true_track_id", "birth", "death", "speed", "family", "length"])
        for t in tracks:
            w.writerow([t.tid, t.birth, t.death, f"{np.linalg.norm(t.velocity):.6f}",
                        t.family_name, f"{t.length:.3f}"])

    n_obs = len(truth_rows)
    print(f"{args.out}.ca: {args.frames} frames, {args.tracks} tracks, {n_obs} observaciones")
    print(f"  densidad media   : {n_obs / args.frames:.1f} segmentos por frame")
    print(f"  nacimientos      : {sum(1 for t in tracks if t.birth > 0)}")
    print(f"  muertes          : {sum(1 for t in tracks if t.death < args.frames - 1)}")
    print(f"  |v| mediana      : {np.median([np.linalg.norm(t.velocity) for t in tracks]):.3f} A/frame")
    print(f"  lineas cerradas por PBC: {sum(1 for t in tracks if t.pbc_closed)} de {len(tracks)}")


if __name__ == "__main__":
    main()
