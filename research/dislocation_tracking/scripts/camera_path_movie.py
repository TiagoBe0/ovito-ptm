#!/usr/bin/env python3
"""Prototipo: renderiza un mp4 con la camara siguiendo una trayectoria arbitraria.

Valida, usando solo el API de scripting de OVITO (sin tocar C++), la idea de
"pasar una trayectoria de camara y exportar a mp4":

  - La trayectoria se define como una lista de puntos de control (frame, pos, target, fov)
    en un CSV, y se interpola (Catmull-Rom si hay >=4 puntos, lineal si no) para cada
    frame de la simulacion.
  - Cada frame se renderiza con ovito.vis.Viewport.render_image() ajustando camera_pos/
    camera_dir/fov a mano, porque Viewport.render_anim() no expone un hook por-frame
    para mover la camara (ver hallazgo abajo).
  - Los frames se acumulan en memoria y se codifican a .mp4 con cv2.VideoWriter, ya que
    esta maquina no tiene ffmpeg instalado como CLI (pero si opencv-python).

Hallazgo de la investigacion: OVITO ya trae toda la maquinaria de fondo (keyframes
genericos via Controller/PRSTransformationController, LookAtController para "camera
target", y VideoEncoder+RenderSettings para mp4/avi). Lo que falta es la capa que
conecta una trayectoria de usuario con esos keyframes. Este script hace esa conexion
al nivel de Python/scripting; una feature nativa en C++ haria lo mismo pero escribiendo
keyframes reales sobre el transformationController del nodo camara, evitando el
render frame-a-frame manual.

Formato del CSV de trayectoria (una fila = un punto de control, no hace falta uno por
frame de la simulacion):

    frame,pos_x,pos_y,pos_z,target_x,target_y,target_z,fov_deg
    0,50,50,150,50,50,50,35
    50,120,50,120,50,50,50,35
    100,50,120,120,50,50,50,45

Uso:
    python3 camera_path_movie.py \
        --input ../data/calib_edge_Au/dump.shear_au.lammpstrj \
        --camera-path camera_path_example.csv \
        --out ../results/camera_path_demo.mp4 \
        --size 640 480 --fps 15
"""
import argparse
import sys

import numpy as np


def catmull_rom(p0, p1, p2, p3, t):
    t2 = t * t
    t3 = t2 * t
    return 0.5 * (
        (2 * p1)
        + (-p0 + p2) * t
        + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2
        + (-p0 + 3 * p1 - 3 * p2 + p3) * t3
    )


class CameraPath:
    """Interpola pos/target/fov definidos en puntos de control dispersos por frame."""

    def __init__(self, control_frames, positions, targets, fovs):
        order = np.argsort(control_frames)
        self.frames = np.asarray(control_frames, dtype=float)[order]
        self.positions = np.asarray(positions, dtype=float)[order]
        self.targets = np.asarray(targets, dtype=float)[order]
        self.fovs = np.asarray(fovs, dtype=float)[order]
        if len(self.frames) < 2:
            raise ValueError("La trayectoria necesita al menos 2 puntos de control")

    def _segment(self, frame):
        f = np.clip(frame, self.frames[0], self.frames[-1])
        i = int(np.searchsorted(self.frames, f, side="right") - 1)
        i = min(max(i, 0), len(self.frames) - 2)
        span = self.frames[i + 1] - self.frames[i]
        t = 0.0 if span == 0 else (f - self.frames[i]) / span
        return i, t

    def _interp_array(self, arr, i, t, use_spline):
        n = len(self.frames)
        if not use_spline or n < 4:
            return arr[i] + (arr[i + 1] - arr[i]) * t
        i0 = max(i - 1, 0)
        i3 = min(i + 2, n - 1)
        return catmull_rom(arr[i0], arr[i], arr[i + 1], arr[i3], t)

    def evaluate(self, frame, use_spline=True):
        i, t = self._segment(frame)
        pos = self._interp_array(self.positions, i, t, use_spline)
        target = self._interp_array(self.targets, i, t, use_spline)
        fov = float(self.fovs[i] + (self.fovs[i + 1] - self.fovs[i]) * t)
        direction = target - pos
        norm = np.linalg.norm(direction)
        if norm < 1e-9:
            raise ValueError(f"pos y target coinciden en frame {frame}")
        return pos, direction / norm, fov


def load_camera_path(csv_path):
    import csv

    frames, positions, targets, fovs = [], [], [], []
    with open(csv_path, newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            frames.append(float(row["frame"]))
            positions.append([float(row["pos_x"]), float(row["pos_y"]), float(row["pos_z"])])
            targets.append([float(row["target_x"]), float(row["target_y"]), float(row["target_z"])])
            fovs.append(float(row.get("fov_deg", 35.0)))
    return CameraPath(frames, positions, targets, fovs)


def qimage_to_bgr(qimage):
    qimage = qimage.convertToFormat(qimage.Format.Format_RGB888)
    width, height = qimage.width(), qimage.height()
    ptr = qimage.constBits()
    arr = np.frombuffer(ptr, dtype=np.uint8, count=height * width * 3)
    rgb = arr.reshape((height, width, 3))
    return rgb[:, :, ::-1].copy()  # RGB -> BGR para cv2


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", required=True, help="Trayectoria de simulacion (dump/xyz/etc.)")
    parser.add_argument("--camera-path", required=True, help="CSV con los puntos de control de la camara")
    parser.add_argument("--out", required=True, help="Archivo .mp4 de salida")
    parser.add_argument("--size", nargs=2, type=int, default=[640, 480], metavar=("W", "H"))
    parser.add_argument("--fps", type=int, default=15)
    parser.add_argument("--frame-range", nargs=2, type=int, default=None, metavar=("FROM", "TO"))
    parser.add_argument("--linear", action="store_true", help="Interpolar linealmente en vez de Catmull-Rom")
    parser.add_argument("--ptm", action="store_true",
                         help="Aplicar Polyhedral Template Matching y colorear atomos por tipo de estructura")
    parser.add_argument("--hide-structures", default=None,
                         help="Nombres PTM a ocultar, separados por coma (p.ej. 'BCC' para resaltar defectos "
                              "en un cristal BCC perfecto). Requiere --ptm")
    args = parser.parse_args()

    import warnings
    warnings.filterwarnings("ignore", message=".*OVITO.*PyPI")
    import cv2
    from ovito.io import import_file
    from ovito.vis import Viewport

    pipeline = import_file(args.input)
    if args.ptm:
        from ovito.modifiers import (PolyhedralTemplateMatchingModifier,
                                      ExpressionSelectionModifier, DeleteSelectedModifier)
        ptm_mod = PolyhedralTemplateMatchingModifier()
        pipeline.modifiers.append(ptm_mod)
        if args.hide_structures:
            wanted = {name.strip().lower() for name in args.hide_structures.split(",")}
            ids = [s.id for s in ptm_mod.structures if s.name.lower() in wanted]
            if ids:
                expression = " || ".join(f"StructureType=={sid}" for sid in ids)
                pipeline.modifiers.append(ExpressionSelectionModifier(expression=expression))
                pipeline.modifiers.append(DeleteSelectedModifier())
    pipeline.add_to_scene()

    num_frames = pipeline.source.num_frames
    start, end = args.frame_range if args.frame_range else (0, num_frames - 1)
    if start < 0 or end >= num_frames or start > end:
        sys.exit(f"Rango de frames invalido: [{start},{end}] (la simulacion tiene {num_frames} frames)")

    path = load_camera_path(args.camera_path)

    vp = Viewport(type=Viewport.Type.Perspective)
    width, height = args.size
    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    writer = cv2.VideoWriter(args.out, fourcc, args.fps, (width, height))
    if not writer.isOpened():
        sys.exit(f"No se pudo abrir el archivo de salida de video: {args.out}")

    try:
        for frame in range(start, end + 1):
            pos, direction, fov_deg = path.evaluate(frame, use_spline=not args.linear)
            vp.camera_pos = pos
            vp.camera_dir = direction
            vp.fov = np.radians(fov_deg)
            try:
                image = vp.render_image(size=(width, height), frame=frame, stop_on_error=True)
            except RuntimeError as exc:
                print(f"frame {frame}/{end}: SALTEADO, error al leer/renderizar ({exc})", flush=True)
                continue
            writer.write(qimage_to_bgr(image))
            print(f"frame {frame}/{end}: pos={pos.round(1)} fov={fov_deg:.1f} deg", flush=True)
    finally:
        writer.release()

    print(f"OK -> {args.out}")


if __name__ == "__main__":
    main()
