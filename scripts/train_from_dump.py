#!/usr/bin/env python3
"""
train_from_dump.py
==================
Train an MLP model from particle data in OVITO dump / trajectory files and
export it as a TorchScript (.pt) file ready to be loaded by the NN Modifier.

The script reads the first frame to discover available property columns,
lets you select which columns to use as input features and which as the
prediction target, then collects data from every frame and trains a
configurable MLP.  Feature normalisation (mean/std) is embedded into the
exported model so the NN Modifier receives raw property values without any
pre-processing step.

Workflow
--------
  1. Load a dump / trajectory file via OVITO's Python API.
  2. (Optional) Apply pipeline modifiers to compute derived properties
     (Voronoi volumes, PTM structure types, CNA types, …).
  3. Inspect the first frame and list all numeric particle properties.
  4. Select feature columns and a target column — interactively or via CLI.
  5. Collect feature-matrix X and target vector y from all (or a subset of)
     frames.
  6. Train a configurable MLP (with embedded normalisation layer).
  7. Export as TorchScript .pt for use with the NN Modifier.

Usage
-----
  # Interactive column selection:
  python train_from_dump.py --input traj.dump

  # Explicit selection, applying PTM first:
  python train_from_dump.py --input traj.dump \\
      --modifiers ptm \\
      --features "Position.X" "Position.Y" "Position.Z" \\
      --target "Structure Type" \\
      --output crystal_model.pt

  # Only list available columns (no training):
  python train_from_dump.py --input traj.dump --modifiers voronoi --list-columns

Requirements
------------
  ovito   (conda install -c ovito ovito  OR  pip install ovito)
  numpy
  torch
"""

import sys
import argparse
import textwrap
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

# ---------------------------------------------------------------------------
# OVITO import (graceful error when not installed)
# ---------------------------------------------------------------------------

try:
    from ovito.io import import_file  # noqa: F401 – checked at import time
except ImportError:
    sys.exit(
        "\nERROR: The 'ovito' Python package is not installed.\n"
        "  conda install -c ovito ovito\n"
        "  or: pip install ovito\n"
    )

# ---------------------------------------------------------------------------
# Supported pipeline modifiers (short name → OVITO class name)
# ---------------------------------------------------------------------------

_MODIFIER_MAP: dict[str, str] = {
    "voronoi": "VoronoiAnalysisModifier",
    "ptm":     "PolyhedralTemplateMatchingModifier",
    "cna":     "CommonNeighborAnalysisModifier",
}


def apply_modifiers(pipeline, keys: list[str]) -> None:
    """Append OVITO modifiers to *pipeline* identified by short name."""
    import ovito.modifiers as om

    for key in keys:
        class_name = _MODIFIER_MAP.get(key.lower())
        if class_name is None:
            print(
                f"  Warning: unknown modifier '{key}'. "
                f"Supported: {', '.join(_MODIFIER_MAP)}"
            )
            continue
        mod_class = getattr(om, class_name, None)
        if mod_class is None:
            print(f"  Warning: {class_name} not found in this OVITO version.")
            continue
        pipeline.modifiers.append(mod_class())
        print(f"  + {class_name}")


# ---------------------------------------------------------------------------
# Column discovery
# ---------------------------------------------------------------------------

# A column is (display_name, property_name, component_index).
# component_index == -1 means scalar property (1 component).
Column = tuple[str, str, int]


def _is_numeric(dtype) -> bool:
    return np.issubdtype(np.dtype(dtype), np.number)


def discover_columns(data) -> list[Column]:
    """Return one Column entry per scalar numeric component in *data.particles*."""
    cols: list[Column] = []
    for prop in data.particles.properties:
        if not _is_numeric(prop.dtype):
            continue
        n = prop.component_count
        if n == 1:
            cols.append((prop.name, prop.name, -1))
        else:
            cnames = (
                list(prop.component_names)
                if prop.component_names
                else [str(c) for c in range(n)]
            )
            for c, cname in enumerate(cnames[:n]):
                cols.append((f"{prop.name}.{cname}", prop.name, c))
    return cols


def find_column(columns: list[Column], name: str) -> int | None:
    """Return the index in *columns* matching *name* (display name or property name)."""
    # 1. Exact display-name match
    for i, (disp, _, _) in enumerate(columns):
        if disp == name:
            return i
    # 2. Property-name match (works for single-component properties)
    for i, (_, pname, comp) in enumerate(columns):
        if pname == name and comp <= 0:
            return i
    return None


def get_column_array(data, col: Column) -> np.ndarray:
    """Return a float32 (N,) array for one column from *data.particles*."""
    _, pname, comp = col
    arr = np.asarray(data.particles[pname], dtype=np.float32)
    if arr.ndim == 2:
        arr = arr[:, comp]
    return arr.ravel()


def get_type_names(data, prop_name: str) -> dict[int, str]:
    """Return {id: name} for a typed particle property, empty dict otherwise."""
    try:
        for prop in data.particles.properties:
            if prop.name == prop_name and hasattr(prop, "types") and prop.types:
                return {t.id: t.name for t in prop.types}
    except Exception:
        pass
    return {}


# ---------------------------------------------------------------------------
# Interactive column selection
# ---------------------------------------------------------------------------

def _print_column_table(columns: list[Column]) -> None:
    print(f"\n  {'#':>4}  {'Display name':<40}  Property / component")
    print(f"  {'─'*4}  {'─'*40}  {'─'*30}")
    for i, (disp, pname, comp) in enumerate(columns):
        comp_str = f"component {comp}" if comp >= 0 else "scalar"
        print(f"  {i:>4}  {disp:<40}  '{pname}' / {comp_str}")


def _parse_indices(raw: str, n: int) -> list[int] | None:
    try:
        indices = [int(x.strip()) for x in raw.replace(",", " ").split() if x.strip()]
        if indices and all(0 <= idx < n for idx in indices):
            return indices
    except ValueError:
        pass
    return None


def interactive_select_features(columns: list[Column]) -> list[int]:
    """Interactively ask the user to pick feature columns. Returns indices."""
    print("\n──── Feature column selection ────")
    _print_column_table(columns)
    print()
    while True:
        raw = input(
            "  Enter feature column numbers (space- or comma-separated): "
        ).strip()
        indices = _parse_indices(raw, len(columns))
        if indices:
            return indices
        print("  Invalid input — please enter valid column numbers.")


def interactive_select_target(columns: list[Column]) -> int:
    """Interactively ask the user to pick a single target column. Returns index."""
    print("\n──── Target column selection ────")
    _print_column_table(columns)
    print()
    while True:
        raw = input("  Enter target column number: ").strip()
        indices = _parse_indices(raw, len(columns))
        if indices and len(indices) == 1:
            return indices[0]
        print("  Please enter exactly one column number.")


# ---------------------------------------------------------------------------
# Data collection across frames
# ---------------------------------------------------------------------------

def collect_data(
    pipeline,
    feat_cols: list[Column],
    tgt_col: Column,
    frame_range: range,
) -> tuple[np.ndarray, np.ndarray]:
    """
    Iterate *frame_range*, extract feature matrix and target vector from each
    frame and concatenate them.

    Returns
    -------
    X : float32 array of shape (total_atoms, n_features)
    y : float32 array of shape (total_atoms,)
    """
    X_parts: list[np.ndarray] = []
    y_parts: list[np.ndarray] = []

    n_frames = len(frame_range)
    for step, frame_idx in enumerate(frame_range):
        data = pipeline.compute(frame_idx)
        n_atoms = data.particles.count

        # Feature matrix for this frame
        feats = np.column_stack(
            [get_column_array(data, col) for col in feat_cols]
        )  # (N, F)

        # Target vector for this frame
        tgt = get_column_array(data, tgt_col)  # (N,)

        X_parts.append(feats)
        y_parts.append(tgt)

        print(
            f"  Frame {frame_idx:5d}  "
            f"({step + 1}/{n_frames})  "
            f"{n_atoms} atoms collected",
            end="\r",
        )

    print()  # newline after \r progress
    X = np.concatenate(X_parts, axis=0)
    y = np.concatenate(y_parts, axis=0)
    return X, y


# ---------------------------------------------------------------------------
# Model definition  (scriptable for TorchScript export)
# ---------------------------------------------------------------------------

class _Normalizer(nn.Module):
    """Embeds feature normalization (mean / std) so the model is self-contained."""

    mean: torch.Tensor
    std: torch.Tensor

    def __init__(self, mean: torch.Tensor, std: torch.Tensor) -> None:
        super().__init__()
        self.register_buffer("mean", mean)
        self.register_buffer("std", std.clamp(min=1e-8))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return (x - self.mean) / self.std


class MLP(nn.Module):
    """MLP with an embedded normalisation layer — directly TorchScript-exportable."""

    def __init__(
        self,
        normalizer: _Normalizer,
        in_features: int,
        hidden_sizes: list[int],
        out_features: int,
    ) -> None:
        super().__init__()
        self.normalizer = normalizer
        layers: list[nn.Module] = []
        prev = in_features
        for h in hidden_sizes:
            layers.append(nn.Linear(prev, h))
            layers.append(nn.ReLU())
            prev = h
        layers.append(nn.Linear(prev, out_features))
        self.net = nn.Sequential(*layers)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.net(self.normalizer(x))


# ---------------------------------------------------------------------------
# Training loop
# ---------------------------------------------------------------------------

def train_model(
    model: MLP,
    X_tr: torch.Tensor,
    y_tr: torch.Tensor,
    X_val: torch.Tensor,
    y_val: torch.Tensor,
    task: str,
    epochs: int,
    lr: float,
    batch_size: int,
) -> None:
    optimizer = torch.optim.Adam(model.parameters(), lr=lr)
    criterion: nn.Module = (
        nn.CrossEntropyLoss() if task == "classification" else nn.MSELoss()
    )

    N = X_tr.shape[0]
    log_every = max(1, epochs // 10)

    for epoch in range(1, epochs + 1):
        model.train()
        perm = torch.randperm(N)
        total_loss = 0.0

        for start in range(0, N, batch_size):
            idx = perm[start : start + batch_size]
            xb, yb = X_tr[idx], y_tr[idx]
            optimizer.zero_grad()
            out = model(xb)
            if task == "classification":
                loss = criterion(out, yb.long())
            else:
                loss = criterion(out.squeeze(-1), yb.float())
            loss.backward()
            optimizer.step()
            total_loss += loss.item() * len(idx)

        if epoch % log_every == 0 or epoch == epochs:
            model.eval()
            with torch.no_grad():
                val_out = model(X_val)
                if task == "classification":
                    preds = val_out.argmax(dim=1)
                    acc = (preds == y_val.long()).float().mean().item()
                    print(
                        f"  epoch {epoch:4d}/{epochs}"
                        f"  train_loss={total_loss / N:.4f}"
                        f"  val_acc={acc:.4f}"
                    )
                else:
                    rmse = (
                        (val_out.squeeze(-1) - y_val.float()).pow(2).mean().sqrt().item()
                    )
                    print(
                        f"  epoch {epoch:4d}/{epochs}"
                        f"  train_loss={total_loss / N:.4f}"
                        f"  val_rmse={rmse:.6f}"
                    )


# ---------------------------------------------------------------------------
# Per-class accuracy report (classification)
# ---------------------------------------------------------------------------

def report_per_class(
    model: MLP,
    X: torch.Tensor,
    y: torch.Tensor,
    class_names: dict[int, str],
    num_classes: int,
) -> None:
    model.eval()
    with torch.no_grad():
        preds = model(X).argmax(dim=1)
    print("\n  Per-class validation accuracy:")
    for k in range(num_classes):
        mask = y.long() == k
        if mask.sum() == 0:
            continue
        acc = (preds[mask] == k).float().mean().item()
        label = class_names.get(k, f"Class {k}")
        print(f"    [{k}] {label:<30}  acc={acc:.4f}  ({mask.sum().item()} samples)")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Train an MLP for the OVITO NN Modifier from dump-file data.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent(
            """\
            Modifier shortcuts (--modifiers):
              voronoi  →  VoronoiAnalysisModifier
              ptm      →  PolyhedralTemplateMatchingModifier
              cna      →  CommonNeighborAnalysisModifier

            Examples:
              # Interactive column selection:
              python train_from_dump.py --input traj.dump

              # Classify by PTM structure type using Voronoi + position features:
              python train_from_dump.py --input traj.dump \\
                  --modifiers ptm voronoi \\
                  --features "Position.X" "Position.Y" "Position.Z" \\
                             "Voronoi Volume" "Coordination" \\
                  --target "Structure Type" \\
                  --output structure_model.pt

              # Only list available columns:
              python train_from_dump.py --input traj.dump --modifiers ptm --list-columns
            """
        ),
    )
    parser.add_argument("--input",   "-i", required=True, metavar="FILE",
                        help="Dump / trajectory file")
    parser.add_argument("--output",  "-o", default="model.pt", metavar="FILE",
                        help="Output TorchScript file (default: model.pt)")
    parser.add_argument("--features", "-f", nargs="+", metavar="COL",
                        help="Feature column display names "
                             "(e.g. 'Position.X' 'Voronoi Volume'). "
                             "Omit for interactive selection.")
    parser.add_argument("--target",  "-t", metavar="COL",
                        help="Target column name (integer → classification, "
                             "float → regression). Omit for interactive selection.")
    parser.add_argument("--task",    choices=["classification", "regression"],
                        help="Override automatic task detection from target dtype.")
    parser.add_argument("--modifiers", nargs="+", metavar="MOD",
                        help="Pipeline modifiers to apply before reading columns.")
    parser.add_argument("--frames",  nargs=2, type=int, metavar=("FIRST", "LAST"),
                        help="Inclusive frame range (default: all frames).")
    parser.add_argument("--hidden-layers", nargs="+", type=int,
                        default=[64, 32], metavar="SIZE",
                        help="Hidden layer sizes (default: 64 32).")
    parser.add_argument("--epochs",    type=int,   default=100)
    parser.add_argument("--lr",        type=float, default=1e-3)
    parser.add_argument("--batch-size", type=int,  default=256)
    parser.add_argument("--val-split",  type=float, default=0.2,
                        help="Validation fraction (default: 0.2).")
    parser.add_argument("--seed",       type=int,  default=0)
    parser.add_argument("--list-columns", action="store_true",
                        help="Print available columns from the first frame and exit.")
    args = parser.parse_args()

    np.random.seed(args.seed)
    torch.manual_seed(args.seed)

    # ── 1. Load pipeline ──────────────────────────────────────────────────
    from ovito.io import import_file

    print(f"\nLoading '{args.input}' …")
    pipeline = import_file(args.input)

    if args.modifiers:
        print("Applying modifiers:")
        apply_modifiers(pipeline, args.modifiers)

    print("Computing first frame …")
    data0 = pipeline.compute(0)
    total_frames = pipeline.num_frames
    print(f"  {data0.particles.count} particles  |  {total_frames} frame(s)")

    columns = discover_columns(data0)
    if not columns:
        sys.exit("ERROR: No numeric particle properties found in the first frame.")

    # ── 2. --list-columns mode ────────────────────────────────────────────
    if args.list_columns:
        print("\nAvailable columns:")
        _print_column_table(columns)
        return

    # ── 3. Feature column selection ───────────────────────────────────────
    if args.features:
        feat_indices: list[int] = []
        for name in args.features:
            idx = find_column(columns, name)
            if idx is None:
                sys.exit(
                    f"ERROR: Feature column '{name}' not found.\n"
                    f"  Run with --list-columns to see available columns."
                )
            feat_indices.append(idx)
    else:
        feat_indices = interactive_select_features(columns)

    feat_cols = [columns[i] for i in feat_indices]
    print(f"\nFeatures ({len(feat_cols)}):")
    for col in feat_cols:
        print(f"  • {col[0]}")

    # ── 4. Target column selection ────────────────────────────────────────
    if args.target:
        tgt_idx = find_column(columns, args.target)
        if tgt_idx is None:
            sys.exit(
                f"ERROR: Target column '{args.target}' not found.\n"
                f"  Run with --list-columns to see available columns."
            )
    else:
        tgt_idx = interactive_select_target(columns)

    tgt_col = columns[tgt_idx]
    print(f"\nTarget: {tgt_col[0]}")

    # ── 5. Detect task type ───────────────────────────────────────────────
    sample_tgt = get_column_array(data0, tgt_col)
    if args.task:
        task = args.task
    else:
        task = (
            "classification"
            if np.issubdtype(sample_tgt.dtype, np.integer)
            else "regression"
        )
    print(f"Task: {task}")

    # For classification, discover class names from typed properties
    type_names: dict[int, str] = {}
    if task == "classification":
        _, tgt_pname, _ = tgt_col
        type_names = get_type_names(data0, tgt_pname)
        if type_names:
            print("  Class labels (from property type definitions):")
            for tid, tname in sorted(type_names.items()):
                print(f"    [{tid}] {tname}")

    # ── 6. Collect data from all frames ───────────────────────────────────
    if args.frames:
        first_f, last_f = args.frames
        frame_range = range(first_f, min(last_f + 1, total_frames))
    else:
        frame_range = range(total_frames)

    print(f"\nCollecting data from {len(frame_range)} frame(s) …")
    X_np, y_np = collect_data(pipeline, feat_cols, tgt_col, frame_range)
    print(f"  Total samples: {X_np.shape[0]}  |  features: {X_np.shape[1]}")

    # For classification, map target values to contiguous 0-based indices
    if task == "classification":
        unique_vals = np.unique(y_np.astype(np.int64))
        val_to_idx = {int(v): i for i, v in enumerate(unique_vals)}
        y_np_mapped = np.array(
            [val_to_idx[int(v)] for v in y_np], dtype=np.int64
        )
        # Rebuild type_names with mapped indices
        type_names_mapped = {
            val_to_idx[k]: v for k, v in type_names.items() if k in val_to_idx
        }
        # Fill any unmapped classes
        for orig_val, mapped_idx in val_to_idx.items():
            if mapped_idx not in type_names_mapped:
                type_names_mapped[mapped_idx] = f"Class {mapped_idx}"
        num_classes = len(unique_vals)
        y_np = y_np_mapped
        type_names = type_names_mapped
        print(f"  Classes: {num_classes}  ({list(unique_vals)} → 0…{num_classes-1})")
    else:
        num_classes = 1

    # ── 7. Train / validation split ───────────────────────────────────────
    N = X_np.shape[0]
    n_val = max(1, int(N * args.val_split))
    idx_perm = np.random.permutation(N)
    val_idx, tr_idx = idx_perm[:n_val], idx_perm[n_val:]

    X_tr = torch.from_numpy(X_np[tr_idx])
    y_tr = torch.from_numpy(y_np[tr_idx].astype(np.float32))
    X_val = torch.from_numpy(X_np[val_idx])
    y_val = torch.from_numpy(y_np[val_idx].astype(np.float32))
    print(f"  Train: {len(tr_idx)}  |  Val: {len(val_idx)}")

    # ── 8. Build model ────────────────────────────────────────────────────
    n_features = X_np.shape[1]
    out_features = num_classes if task == "classification" else 1

    mean = torch.from_numpy(X_np[tr_idx].mean(axis=0).astype(np.float32))
    std  = torch.from_numpy(X_np[tr_idx].std(axis=0).astype(np.float32))
    normalizer = _Normalizer(mean, std)

    model = MLP(normalizer, n_features, args.hidden_layers, out_features)
    n_params = sum(p.numel() for p in model.parameters())
    print(
        f"\nModel: {n_features} → "
        + " → ".join(str(h) for h in args.hidden_layers)
        + f" → {out_features}  ({n_params} parameters)"
    )

    # ── 9. Train ──────────────────────────────────────────────────────────
    print(f"\nTraining  (epochs={args.epochs}, lr={args.lr}, batch={args.batch_size}) …")
    train_model(
        model, X_tr, y_tr, X_val, y_val,
        task=task,
        epochs=args.epochs,
        lr=args.lr,
        batch_size=args.batch_size,
    )

    if task == "classification":
        report_per_class(model, X_val, y_val, type_names, num_classes)

    # ── 10. Export as TorchScript ─────────────────────────────────────────
    output_path = Path(args.output)
    model.eval()
    scripted = torch.jit.script(model)
    scripted.save(str(output_path))
    print(f"\nModel saved: {output_path.resolve()}")

    # ── 11. Usage instructions ────────────────────────────────────────────
    print("\n" + "─" * 60)
    print("HOW TO USE THIS MODEL IN OVITO")
    print("─" * 60)
    print("1. Add the 'NN Modifier' to your pipeline.")
    print(f"2. Set Model path to: {output_path.resolve()}")
    print("3. Select Input features → 'Particle property columns'.")
    print("   Enable these columns IN THIS ORDER:")
    for col in feat_cols:
        print(f"     • {col[0]}")
    print(f"4. Set Output mode → '{task.capitalize()}'.")
    if task == "classification" and type_names:
        print("5. Set Class labels (one per line):")
        for k in sorted(type_names):
            print(f"     {type_names[k]}")
    print("─" * 60 + "\n")


if __name__ == "__main__":
    main()
