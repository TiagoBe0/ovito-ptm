#!/usr/bin/env python3
"""Genera trayectorias con velocidad de dislocacion CONOCIDA DE ANTEMANO.

El tracker mide la velocidad de cada dislocacion; para validarlo hace falta un caso
donde esa velocidad no se deduzca de la dinamica sino que este impuesta. Se generan
dos, complementarios:

  translate : se toma una configuracion relajada de MD (con sus 4 parciales de
              Shockley) y se traslada rigidamente en x una cantidad fija por frame.
              Las lineas se mueven exactamente dx por frame en el marco del
              laboratorio: verdad de terreno exacta a precision de maquina. Prueba
              identidad entre frames, desenrollado por PBC, ajuste de deriva y
              proyeccion perpendicular a la linea. Con --noise se agrega ruido
              gaussiano por frame para que DXA no vea dos veces la misma imagen.

  glide     : se inserta el campo elastico de una dislocacion de borde con atomsk en
              posiciones x crecientes sobre una RED FIJA. Aca la dislocacion se mueve
              respecto del cristal, que es el caso fisicamente relevante (glide, no
              traslacion del material). La verdad de terreno es la pendiente impuesta.

Uso:
    python3 make_known_case.py translate --source dump.shear_au.lammpstrj --dx 0.2 --frames 150
    python3 make_known_case.py glide --super au_super.lmp --dx 0.5 --frames 60
"""
import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np


ATOMSK = Path.home() / "miniconda3/bin/atomsk"


def read_first_frame(path):
    """Devuelve (cabecera hasta ITEM: ATOMS inclusive, columnas, array de datos)."""
    header, columns, rows = [], None, []
    with open(path) as f:
        for line in f:
            header.append(line)
            if line.startswith("ITEM: ATOMS"):
                columns = line.split()[2:]
                break
        natoms = int(header[3])
        for _ in range(natoms):
            rows.append(f.readline().split())
    return header, columns, np.array(rows, dtype=float)


def write_frame(out, header, data, timestep):
    """Escribe un frame de dump reemplazando el timestep de la cabecera."""
    for i, line in enumerate(header):
        out.write(f"{timestep}\n" if i == 1 else line)
    for row in data:
        out.write(f"{int(row[0])} {int(row[1])} " + " ".join(f"{v:.5f}" for v in row[2:]) + "\n")


def make_translate(args):
    header, columns, data = read_first_frame(args.source)
    ix = columns.index("x")
    # Limites de la caja en x, para reenvolver despues de trasladar.
    xlo, xhi = (float(v) for v in header[5].split()[:2])
    lx = xhi - xlo

    rng = np.random.default_rng(args.seed)
    out_path = Path(args.out or "dump.known_translate.lammpstrj")
    with open(out_path, "w") as out:
        for i in range(args.frames):
            frame = data.copy()
            frame[:, ix] += i * args.dx
            if args.noise > 0:
                frame[:, ix:ix + 3] += rng.normal(0.0, args.noise, size=(len(frame), 3))
            frame[:, ix] = xlo + np.mod(frame[:, ix] - xlo, lx)
            write_frame(out, header, frame, i)
    print(f"{out_path}: {args.frames} frames, dx = {args.dx} A/frame")
    print(f"velocidad impuesta = {args.dx / args.dt:.4f} A/ps = {args.dx / args.dt * 100:.1f} m/s "
          f"(con --dt {args.dt})")


def make_glide(args):
    """Cada frame es una insercion independiente de atomsk en x = x0 + i*dx."""
    tmp = Path("_glide_tmp.lmp")
    out_path = Path(args.out or "dump.known_glide.lammpstrj")
    frames = []
    for i in range(args.frames):
        x = args.x0 + i * args.dx
        cmd = [str(ATOMSK), args.super, "-disloc", f"{x}", f"{args.y0}",
               "edge_rm", "Z", "Y", f"{args.b}", f"{args.nu}", str(tmp), "-ow"]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if not tmp.exists():
            sys.exit(f"atomsk fallo en x={x}:\n{res.stdout[-800:]}")
        frames.append(read_lmp(tmp))
        tmp.unlink()
        print(f"\rframe {i + 1}/{args.frames} (x = {x:.2f} A)", end="", flush=True)
    print()

    with open(out_path, "w") as out:
        for i, (box, atoms) in enumerate(frames):
            out.write(f"ITEM: TIMESTEP\n{i}\nITEM: NUMBER OF ATOMS\n{len(atoms)}\n")
            out.write("ITEM: BOX BOUNDS pp pp pp\n")
            for lo, hi in box:
                out.write(f"{lo:.10e} {hi:.10e}\n")
            out.write("ITEM: ATOMS id type x y z\n")
            for j, (t, xx, yy, zz) in enumerate(atoms, start=1):
                out.write(f"{j} {int(t)} {xx:.5f} {yy:.5f} {zz:.5f}\n")
    print(f"{out_path}: {args.frames} frames, dx = {args.dx} A/frame")
    print(f"velocidad impuesta = {args.dx / args.dt:.4f} A/ps = {args.dx / args.dt * 100:.1f} m/s "
          f"(con --dt {args.dt})")


def read_lmp(path):
    """Lee un LAMMPS data file de atomsk: devuelve (limites de caja, [(tipo,x,y,z)])."""
    box, atoms = [], []
    with open(path) as f:
        lines = f.readlines()
    for line in lines:
        if "xlo xhi" in line or "ylo yhi" in line or "zlo zhi" in line:
            lo, hi = (float(v) for v in line.split()[:2])
            box.append((lo, hi))
    start = next(i for i, line in enumerate(lines) if line.strip().startswith("Atoms")) + 2
    for line in lines[start:]:
        parts = line.split()
        if len(parts) < 5:
            break
        atoms.append((float(parts[1]), float(parts[2]), float(parts[3]), float(parts[4])))
    return box, atoms


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="mode", required=True)

    t = sub.add_parser("translate")
    t.add_argument("--source", required=True, help="dump de MD del que se toma el primer frame")
    t.add_argument("--dx", type=float, default=0.2, help="desplazamiento por frame, A")
    t.add_argument("--frames", type=int, default=150)
    t.add_argument("--dt", type=float, default=0.1, help="tiempo por frame, ps (solo para informar)")
    t.add_argument("--noise", type=float, default=0.0, help="sigma del ruido gaussiano por frame, A")
    t.add_argument("--seed", type=int, default=12345)
    t.add_argument("--out")
    t.set_defaults(func=make_translate)

    g = sub.add_parser("glide")
    g.add_argument("--super", required=True, help="supercelda perfecta (au_super.lmp)")
    g.add_argument("--dx", type=float, default=0.5)
    g.add_argument("--frames", type=int, default=60)
    g.add_argument("--dt", type=float, default=0.25)
    g.add_argument("--x0", type=float, default=20.0)
    g.add_argument("--y0", type=float, default=49.443)
    g.add_argument("--b", type=float, default=2.88358)
    g.add_argument("--nu", type=float, default=0.42)
    g.add_argument("--out")
    g.set_defaults(func=make_glide)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
