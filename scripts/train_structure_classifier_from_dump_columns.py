#!/usr/bin/env python3
"""
Train a TorchScript classifier for OVITO MLStructureModifier in
"Particle property columns" mode using LAMMPS dump files.

The model input dimension is the number of selected particle properties.
This must match exactly the columns selected in the OVITO modifier panel.

Example
-------
python scripts/train_structure_classifier_from_dump_columns.py \
  --data-root ./dataset \
  --features Coordination AtomicVolume CavityRadius StructureType \
  --label-source parent_dir \
  --output model_property_columns.pt
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

import numpy as np
import torch
import torch.nn as nn


@dataclass
class DumpFrame:
    columns: List[str]
    data: np.ndarray


def _read_dump_frames(path: Path) -> Iterable[DumpFrame]:
    """Yield all ITEM: ATOMS frames found in a LAMMPS dump file."""
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    i = 0
    n = len(lines)
    while i < n:
        if not lines[i].startswith("ITEM: TIMESTEP"):
            i += 1
            continue

        i += 1  # timestep value
        if i >= n:
            break
        i += 1

        if i >= n or not lines[i].startswith("ITEM: NUMBER OF ATOMS"):
            continue
        i += 1
        if i >= n:
            break
        natoms = int(lines[i].strip())
        i += 1

        if i >= n or not lines[i].startswith("ITEM: BOX BOUNDS"):
            continue
        i += 4  # header + 3 bounds lines

        if i >= n or not lines[i].startswith("ITEM: ATOMS"):
            continue
        header = lines[i].strip().split()
        columns = header[2:]
        i += 1

        rows: List[List[float]] = []
        for _ in range(natoms):
            if i >= n:
                break
            rows.append([float(x) for x in lines[i].split()])
            i += 1

        if len(rows) == natoms and rows:
            yield DumpFrame(columns=columns, data=np.asarray(rows, dtype=np.float32))


class ColumnMLP(nn.Module):
    def __init__(self, n_features: int, n_classes: int):
        super().__init__()
        self.register_buffer("feat_mean", torch.zeros(n_features))
        self.register_buffer("feat_std", torch.ones(n_features))
        self.net = nn.Sequential(
            nn.Linear(n_features, 64),
            nn.ReLU(),
            nn.Linear(64, 64),
            nn.ReLU(),
            nn.Linear(64, n_classes),
        )

    def set_normalization(self, mean: np.ndarray, std: np.ndarray) -> None:
        self.feat_mean.copy_(torch.from_numpy(mean.astype(np.float32)))
        self.feat_std.copy_(torch.from_numpy(std.astype(np.float32)))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = (x - self.feat_mean) / self.feat_std
        return self.net(x)


def _collect_dataset(
    files: Sequence[Path],
    features: Sequence[str],
    label_source: str,
    label_column: str,
) -> Tuple[np.ndarray, np.ndarray, Dict[str, int]]:
    feature_rows: List[np.ndarray] = []
    labels: List[np.ndarray] = []
    class_map: Dict[str, int] = {}

    for dump_file in files:
        class_name = dump_file.parent.name
        if label_source == "parent_dir" and class_name not in class_map:
            class_map[class_name] = len(class_map)

        for frame in _read_dump_frames(dump_file):
            col_to_idx = {c: idx for idx, c in enumerate(frame.columns)}
            missing = [c for c in features if c not in col_to_idx]
            if missing:
                raise ValueError(f"{dump_file}: missing feature columns: {missing}")

            Xf = np.stack([frame.data[:, col_to_idx[c]] for c in features], axis=1)
            feature_rows.append(Xf.astype(np.float32))

            if label_source == "parent_dir":
                y = np.full(len(Xf), class_map[class_name], dtype=np.int64)
            else:
                if label_column not in col_to_idx:
                    raise ValueError(f"{dump_file}: missing label column '{label_column}'")
                y = frame.data[:, col_to_idx[label_column]].astype(np.int64)
            labels.append(y)

    if not feature_rows:
        raise ValueError("No samples were found in the provided dataset.")

    X = np.concatenate(feature_rows, axis=0)
    y = np.concatenate(labels, axis=0)

    if label_source == "column":
        uniq = sorted(int(v) for v in np.unique(y))
        class_map = {str(v): i for i, v in enumerate(uniq)}
        remap = np.empty(max(uniq) + 1, dtype=np.int64)
        for k, i in class_map.items():
            remap[int(k)] = i
        y = remap[y]

    return X, y, class_map


def _train(
    model: ColumnMLP,
    X_train: torch.Tensor,
    y_train: torch.Tensor,
    X_val: torch.Tensor,
    y_val: torch.Tensor,
    epochs: int,
    batch_size: int,
    lr: float,
) -> None:
    optimizer = torch.optim.Adam(model.parameters(), lr=lr)
    criterion = nn.CrossEntropyLoss()
    loader = torch.utils.data.DataLoader(
        torch.utils.data.TensorDataset(X_train, y_train),
        batch_size=batch_size,
        shuffle=True,
    )

    for epoch in range(1, epochs + 1):
        model.train()
        running_loss = 0.0
        for xb, yb in loader:
            optimizer.zero_grad()
            loss = criterion(model(xb), yb)
            loss.backward()
            optimizer.step()
            running_loss += loss.item() * len(xb)

        if epoch == 1 or epoch % 10 == 0 or epoch == epochs:
            model.eval()
            with torch.no_grad():
                pred = model(X_val).argmax(dim=1)
                acc = (pred == y_val).float().mean().item()
            print(
                f"Epoch {epoch:3d}/{epochs} | "
                f"loss={running_loss/len(X_train):.4f} | val_acc={acc*100:.2f}%"
            )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, required=True,
                        help="Root directory containing .dump files.")
    parser.add_argument("--glob", default="**/*.dump",
                        help="Glob pattern under --data-root (default: **/*.dump).")
    parser.add_argument("--features", nargs="+",
                        default=["Coordination", "AtomicVolume", "CavityRadius", "StructureType"],
                        help="Feature columns to use as model input.")
    parser.add_argument("--label-source", choices=["parent_dir", "column"], default="parent_dir",
                        help="How to build labels: directory name or a dump column.")
    parser.add_argument("--label-column", default="StructureType",
                        help="Label column name when --label-source=column.")
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--val-fraction", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--output", type=Path, default=Path("model_property_columns.pt"))
    parser.add_argument("--metadata", type=Path, default=Path("model_property_columns.metadata.json"),
                        help="JSON file storing feature order and class mapping.")
    args = parser.parse_args()

    dump_files = sorted(args.data_root.glob(args.glob))
    if not dump_files:
        raise SystemExit(f"No dump files found in {args.data_root} with pattern '{args.glob}'")

    print(f"Found {len(dump_files)} dump files")
    X, y, class_map = _collect_dataset(
        files=dump_files,
        features=args.features,
        label_source=args.label_source,
        label_column=args.label_column,
    )
    print(f"Samples: {len(X):,} | Features: {X.shape[1]} | Classes: {len(np.unique(y))}")

    rng = np.random.default_rng(args.seed)
    idx = rng.permutation(len(X))
    split = int((1.0 - args.val_fraction) * len(idx))
    tr, va = idx[:split], idx[split:]

    mean = X[tr].mean(axis=0)
    std = X[tr].std(axis=0)
    std[std < 1e-8] = 1.0

    X_train = torch.from_numpy(X[tr])
    y_train = torch.from_numpy(y[tr])
    X_val = torch.from_numpy(X[va])
    y_val = torch.from_numpy(y[va])

    model = ColumnMLP(n_features=len(args.features), n_classes=int(y.max()) + 1)
    model.set_normalization(mean, std)

    _train(
        model=model,
        X_train=X_train,
        y_train=y_train,
        X_val=X_val,
        y_val=y_val,
        epochs=args.epochs,
        batch_size=args.batch_size,
        lr=args.lr,
    )

    model.eval()
    scripted = torch.jit.script(model)
    scripted.save(str(args.output))

    metadata = {
        "features": args.features,
        "label_source": args.label_source,
        "label_column": args.label_column,
        "class_map": class_map,
        "n_features": len(args.features),
        "n_classes": int(y.max()) + 1,
    }
    args.metadata.write_text(json.dumps(metadata, indent=2), encoding="utf-8")

    print(f"Saved TorchScript model to: {args.output.resolve()}")
    print(f"Saved metadata to: {args.metadata.resolve()}")
    print("\nOVITO note:")
    print("- In MLStructureModifier set Input mode = Particle property columns")
    print("- Select columns in EXACTLY this order:")
    print("  " + ", ".join(args.features))
    print(f"- Number of selected columns must be {len(args.features)}")


if __name__ == "__main__":
    main()
