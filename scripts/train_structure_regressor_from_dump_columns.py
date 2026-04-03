#!/usr/bin/env python3
"""
Train a TorchScript regressor for OVITO MLStructureModifier in
"Particle property columns" mode using LAMMPS dump files.

This script is intended for the new OutputMode=Regression path:
- Input: selected per-particle columns (features)
- Output: one or more continuous per-particle values (targets)

The model input dimension must match the number of columns selected in OVITO.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple

import numpy as np
import torch
import torch.nn as nn


@dataclass
class DumpFrame:
    columns: List[str]
    data: np.ndarray


def _read_dump_frames(path: Path) -> Iterable[DumpFrame]:
    """Yield ITEM: ATOMS frames from a LAMMPS dump file."""
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    i = 0
    n = len(lines)

    while i < n:
        if not lines[i].startswith("ITEM: TIMESTEP"):
            i += 1
            continue

        i += 2  # header + timestep value
        if i >= n or not lines[i].startswith("ITEM: NUMBER OF ATOMS"):
            continue
        i += 1
        if i >= n:
            break
        natoms = int(lines[i].strip())
        i += 1

        if i >= n or not lines[i].startswith("ITEM: BOX BOUNDS"):
            continue
        i += 4

        if i >= n or not lines[i].startswith("ITEM: ATOMS"):
            continue
        header = lines[i].split()
        columns = header[2:]
        i += 1

        rows: List[List[float]] = []
        for _ in range(natoms):
            if i >= n:
                break
            rows.append([float(v) for v in lines[i].split()])
            i += 1

        if len(rows) == natoms and rows:
            yield DumpFrame(columns=columns, data=np.asarray(rows, dtype=np.float32))


class ColumnRegressor(nn.Module):
    def __init__(self, n_features: int, n_outputs: int):
        super().__init__()
        self.register_buffer("feat_mean", torch.zeros(n_features))
        self.register_buffer("feat_std", torch.ones(n_features))
        self.register_buffer("tgt_mean", torch.zeros(n_outputs))
        self.register_buffer("tgt_std", torch.ones(n_outputs))
        self.net = nn.Sequential(
            nn.Linear(n_features, 64),
            nn.ReLU(),
            nn.Linear(64, 64),
            nn.ReLU(),
            nn.Linear(64, n_outputs),
        )

    def set_normalization(
        self,
        feat_mean: np.ndarray,
        feat_std: np.ndarray,
        tgt_mean: np.ndarray,
        tgt_std: np.ndarray,
    ) -> None:
        self.feat_mean.copy_(torch.from_numpy(feat_mean.astype(np.float32)))
        self.feat_std.copy_(torch.from_numpy(feat_std.astype(np.float32)))
        self.tgt_mean.copy_(torch.from_numpy(tgt_mean.astype(np.float32)))
        self.tgt_std.copy_(torch.from_numpy(tgt_std.astype(np.float32)))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = (x - self.feat_mean) / self.feat_std
        y = self.net(x)
        y = y * self.tgt_std + self.tgt_mean
        if y.shape[1] == 1:
            return y.squeeze(1)  # [N,1] -> [N]
        return y               # [N,K]


def _collect_dataset(
    files: Sequence[Path],
    feature_columns: Sequence[str],
    target_columns: Sequence[str],
) -> Tuple[np.ndarray, np.ndarray]:
    all_x: List[np.ndarray] = []
    all_y: List[np.ndarray] = []

    for dump_file in files:
        for frame in _read_dump_frames(dump_file):
            col_to_idx = {c: i for i, c in enumerate(frame.columns)}

            missing_features = [c for c in feature_columns if c not in col_to_idx]
            if missing_features:
                raise ValueError(f"{dump_file}: missing feature columns {missing_features}")

            missing_targets = [c for c in target_columns if c not in col_to_idx]
            if missing_targets:
                raise ValueError(f"{dump_file}: missing target columns {missing_targets}")

            x = np.stack([frame.data[:, col_to_idx[c]] for c in feature_columns], axis=1)
            y = np.stack([frame.data[:, col_to_idx[c]] for c in target_columns], axis=1)

            all_x.append(x.astype(np.float32))
            all_y.append(y.astype(np.float32))

    if not all_x:
        raise ValueError("No samples were found in the provided dump files.")

    return np.concatenate(all_x, axis=0), np.concatenate(all_y, axis=0)


def _evaluate_metrics(pred: torch.Tensor, target: torch.Tensor) -> Tuple[float, float]:
    mse = torch.mean((pred - target) ** 2).item()
    mae = torch.mean(torch.abs(pred - target)).item()
    return mse, mae


def _train(
    model: ColumnRegressor,
    x_train: torch.Tensor,
    y_train: torch.Tensor,
    x_val: torch.Tensor,
    y_val: torch.Tensor,
    epochs: int,
    batch_size: int,
    lr: float,
) -> None:
    opt = torch.optim.Adam(model.parameters(), lr=lr)
    loss_fn = nn.MSELoss()
    loader = torch.utils.data.DataLoader(
        torch.utils.data.TensorDataset(x_train, y_train),
        batch_size=batch_size,
        shuffle=True,
    )

    for epoch in range(1, epochs + 1):
        model.train()
        total_loss = 0.0
        for xb, yb in loader:
            opt.zero_grad()
            pred = model(xb)
            if pred.dim() == 1:
                pred = pred.unsqueeze(1)
            loss = loss_fn(pred, yb)
            loss.backward()
            opt.step()
            total_loss += loss.item() * len(xb)

        if epoch == 1 or epoch % 10 == 0 or epoch == epochs:
            model.eval()
            with torch.no_grad():
                val_pred = model(x_val)
                if val_pred.dim() == 1:
                    val_pred = val_pred.unsqueeze(1)
                mse, mae = _evaluate_metrics(val_pred, y_val)
            print(
                f"Epoch {epoch:3d}/{epochs} | "
                f"train_mse={total_loss/len(x_train):.6f} | "
                f"val_mse={mse:.6f} | val_mae={mae:.6f}"
            )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, required=True,
                        help="Root directory containing .dump files.")
    parser.add_argument("--glob", default="**/*.dump",
                        help="Glob pattern under --data-root (default: **/*.dump).")
    parser.add_argument("--features", nargs="+",
                        default=["Coordination", "AtomicVolume", "CavityRadius", "StructureType"],
                        help="Input feature columns.")
    parser.add_argument("--targets", nargs="+", required=True,
                        help="Target column(s) for regression output.")
    parser.add_argument("--epochs", type=int, default=60)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--val-fraction", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--output", type=Path, default=Path("model_property_columns_regression.pt"))
    parser.add_argument("--metadata", type=Path,
                        default=Path("model_property_columns_regression.metadata.json"),
                        help="JSON file storing feature/target order and model dimensions.")
    args = parser.parse_args()

    files = sorted(args.data_root.glob(args.glob))
    if not files:
        raise SystemExit(f"No dump files found in {args.data_root} with pattern '{args.glob}'")

    print(f"Found {len(files)} dump files")
    X, Y = _collect_dataset(files, args.features, args.targets)
    print(f"Samples: {len(X):,} | Features: {X.shape[1]} | Targets: {Y.shape[1]}")

    rng = np.random.default_rng(args.seed)
    idx = rng.permutation(len(X))
    split = int((1.0 - args.val_fraction) * len(X))
    tr, va = idx[:split], idx[split:]

    x_mean = X[tr].mean(axis=0)
    x_std = X[tr].std(axis=0)
    x_std[x_std < 1e-8] = 1.0

    y_mean = Y[tr].mean(axis=0)
    y_std = Y[tr].std(axis=0)
    y_std[y_std < 1e-8] = 1.0

    x_train = torch.from_numpy(X[tr])
    y_train = torch.from_numpy(Y[tr])
    x_val = torch.from_numpy(X[va])
    y_val = torch.from_numpy(Y[va])

    train_targets_norm = (y_train - torch.from_numpy(y_mean)) / torch.from_numpy(y_std)
    val_targets_norm = (y_val - torch.from_numpy(y_mean)) / torch.from_numpy(y_std)

    model = ColumnRegressor(n_features=len(args.features), n_outputs=len(args.targets))
    model.set_normalization(x_mean, x_std, y_mean, y_std)

    _train(
        model=model,
        x_train=x_train,
        y_train=train_targets_norm,
        x_val=x_val,
        y_val=val_targets_norm,
        epochs=args.epochs,
        batch_size=args.batch_size,
        lr=args.lr,
    )

    model.eval()
    scripted = torch.jit.script(model)
    scripted.save(str(args.output))

    metadata = {
        "mode": "regression",
        "features": args.features,
        "targets": args.targets,
        "n_features": len(args.features),
        "n_targets": len(args.targets),
    }
    args.metadata.write_text(json.dumps(metadata, indent=2), encoding="utf-8")

    print(f"Saved TorchScript model to: {args.output.resolve()}")
    print(f"Saved metadata to: {args.metadata.resolve()}")
    print("\nOVITO note:")
    print("- Set Input mode = Particle property columns")
    print("- Set Output mode = Regression")
    print("- Select input columns in EXACTLY this order:")
    print("  " + ", ".join(args.features))
    print(f"- Expected model outputs: {len(args.targets)}")
    if len(args.targets) == 1:
        print("  (model returns [N] scalar output)")
    else:
        print("  (model returns [N, K] vector output)")


if __name__ == "__main__":
    main()
