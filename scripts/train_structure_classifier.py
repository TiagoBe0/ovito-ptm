#!/usr/bin/env python3
"""
train_structure_classifier.py
==============================
Train a simple MLP that classifies atomic crystal structures from rotation-
invariant descriptors (sorted cutoff-neighbor distances), then export the
model as a TorchScript file (.pt) that can be loaded by MLStructureModifier.

Usage
-----
    python scripts/train_structure_classifier.py [--output model.pt]

Requirements
------------
    numpy, torch  (no ASE needed)

Descriptor
----------
For each atom i, collect all neighbor distances within CUTOFF, sort them
ascending, pad with CUTOFF if fewer than MAX_NEIGH neighbors, truncate if
more.  Divide by CUTOFF so features lie in [0, 1].
Shape: [MAX_NEIGH]

This is invariant to rotation, translation, and permutation of the neighbor
list.  It is *not* fully permutation-invariant in a deep sense, but sorted
distances are already sufficient to distinguish the 5 ideal structures below.

Classes
-------
  0  FCC   (face-centred cubic,    a = 3.52 Å, Ni-like)
  1  HCP   (hexagonal close-packed, a = 2.51 Å, Mg-like)
  2  BCC   (body-centred cubic,    a = 2.87 Å, Fe-like)
  3  Diamond (cubic diamond,       a = 5.43 Å, Si-like)
  4  SC    (simple cubic,          a = 2.50 Å)
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn


# ---------------------------------------------------------------------------
# Hyper-parameters – must match the C++ MLStructureModifier defaults
# ---------------------------------------------------------------------------

CUTOFF     = 5.0   # Å  (matches MLStructureModifier::cutoffRadius default)
MAX_NEIGH  = 16    # (matches MLStructureModifier::numNeighbors default)
NUM_CLASSES = 5
CLASS_NAMES = ["FCC", "HCP", "BCC", "Diamond", "SC"]

SIGMA_THERMAL = 0.10   # Å – Gaussian displacement noise to mimic thermal motion
N_CONFIGS     = 300    # number of noisy configurations per structure type
EPOCHS        = 60
LR            = 1e-3
BATCH_SIZE    = 256


# ===========================================================================
# Crystal generators
# ===========================================================================

def _apply_pbc(delta: np.ndarray, box: np.ndarray) -> np.ndarray:
    """Minimum-image convention for an orthorhombic box."""
    return delta - box * np.round(delta / box)


def _neighbor_distances(pos: np.ndarray, box: np.ndarray) -> np.ndarray:
    """
    Compute per-atom sorted neighbor distance descriptors [N, MAX_NEIGH].

    Parameters
    ----------
    pos : (N, 3) array of Cartesian positions
    box : (3,)  box lengths (orthorhombic, periodic in all directions)
    """
    N = len(pos)
    descriptors = np.full((N, MAX_NEIGH), CUTOFF, dtype=np.float32)

    for i in range(N):
        delta = pos - pos[i]               # (N, 3)
        delta = _apply_pbc(delta, box)     # minimum image
        dists = np.linalg.norm(delta, axis=1)
        dists[i] = np.inf                  # exclude self
        mask = dists < CUTOFF
        d = np.sort(dists[mask])           # ascending
        k = min(len(d), MAX_NEIGH)
        descriptors[i, :k] = d[:k]

    return descriptors / CUTOFF            # normalise to [0, 1]


def fcc_positions(a: float = 3.52, repeat: int = 4):
    """FCC conventional cubic cell repeated repeat^3 times."""
    basis = np.array([[0, 0, 0], [0.5, 0.5, 0],
                      [0.5, 0, 0.5], [0, 0.5, 0.5]], dtype=float) * a
    pos = []
    for ix in range(repeat):
        for iy in range(repeat):
            for iz in range(repeat):
                shift = np.array([ix, iy, iz], dtype=float) * a
                for b in basis:
                    pos.append(b + shift)
    box = np.full(3, repeat * a)
    return np.array(pos), box


def bcc_positions(a: float = 2.87, repeat: int = 5):
    """BCC conventional cubic cell repeated repeat^3 times."""
    basis = np.array([[0, 0, 0], [0.5, 0.5, 0.5]], dtype=float) * a
    pos = []
    for ix in range(repeat):
        for iy in range(repeat):
            for iz in range(repeat):
                shift = np.array([ix, iy, iz], dtype=float) * a
                for b in basis:
                    pos.append(b + shift)
    box = np.full(3, repeat * a)
    return np.array(pos), box


def hcp_positions(a: float = 2.51, nx: int = 5, ny: int = 5, nz: int = 4):
    """
    HCP using an orthorhombic supercell (4 atoms per cell):
        a_orth = a,  b_orth = a√3,  c_orth = c = a * 1.633
    Fractional basis:
        (0,   0,   0),  (1/2, 1/2, 0),
        (1/2, 1/6, 1/2),(0,   2/3, 1/2)
    """
    c = a * 1.6330
    sq3 = np.sqrt(3.0)
    ax, ay, az = a, a * sq3, c
    basis_frac = np.array([
        [0.0,       0.0,       0.0],
        [0.5,       0.5,       0.0],
        [0.5,       1.0 / 6.0, 0.5],
        [0.0,       2.0 / 3.0, 0.5],
    ])
    basis = basis_frac * np.array([ax, ay, az])
    pos = []
    for ix in range(nx):
        for iy in range(ny):
            for iz in range(nz):
                shift = np.array([ix * ax, iy * ay, iz * az])
                for b in basis:
                    pos.append(b + shift)
    box = np.array([nx * ax, ny * ay, nz * az])
    return np.array(pos), box


def diamond_positions(a: float = 5.43, repeat: int = 3):
    """Cubic diamond structure (FCC + 2-atom basis)."""
    basis = np.array([
        [0.00, 0.00, 0.00],
        [0.50, 0.50, 0.00],
        [0.50, 0.00, 0.50],
        [0.00, 0.50, 0.50],
        [0.25, 0.25, 0.25],
        [0.75, 0.75, 0.25],
        [0.75, 0.25, 0.75],
        [0.25, 0.75, 0.75],
    ], dtype=float) * a
    pos = []
    for ix in range(repeat):
        for iy in range(repeat):
            for iz in range(repeat):
                shift = np.array([ix, iy, iz], dtype=float) * a
                for b in basis:
                    pos.append(b + shift)
    box = np.full(3, repeat * a)
    return np.array(pos), box


def sc_positions(a: float = 2.50, repeat: int = 7):
    """Simple cubic lattice."""
    pos = []
    for ix in range(repeat):
        for iy in range(repeat):
            for iz in range(repeat):
                pos.append(np.array([ix, iy, iz], dtype=float) * a)
    box = np.full(3, repeat * a)
    return np.array(pos), box


# ===========================================================================
# Dataset builder
# ===========================================================================

def build_dataset(verbose: bool = True):
    """
    Generate training data for all 5 structure classes.
    Returns X (float32 array [total_atoms, MAX_NEIGH]) and
            y (int64  array [total_atoms]).
    """
    generators = [
        ("FCC",     0, fcc_positions),
        ("HCP",     1, hcp_positions),
        ("BCC",     2, bcc_positions),
        ("Diamond", 3, diamond_positions),
        ("SC",      4, sc_positions),
    ]

    all_X, all_y = [], []
    rng = np.random.default_rng(42)

    for name, label, gen_fn in generators:
        pos_ideal, box = gen_fn()
        n_atoms = len(pos_ideal)

        for cfg_idx in range(N_CONFIGS):
            # Add Gaussian thermal noise
            noise = rng.normal(0, SIGMA_THERMAL, size=pos_ideal.shape)
            pos = (pos_ideal + noise) % box   # keep inside box (PBC wrap)

            desc = _neighbor_distances(pos, box)    # [N, MAX_NEIGH]
            all_X.append(desc)
            all_y.append(np.full(n_atoms, label, dtype=np.int64))

        if verbose:
            print(f"  {name:8s}: {N_CONFIGS} configs × {n_atoms:4d} atoms "
                  f"= {N_CONFIGS * n_atoms:7d} samples")

    X = np.concatenate(all_X, axis=0)   # [total, MAX_NEIGH]
    y = np.concatenate(all_y, axis=0)   # [total]
    return X, y


# ===========================================================================
# Model definition
# ===========================================================================

class StructureClassifier(nn.Module):
    """
    3-layer MLP: MAX_NEIGH → 64 → 32 → NUM_CLASSES
    Input  : sorted normalised neighbor distances  [N, MAX_NEIGH]
    Output : class logits                          [N, NUM_CLASSES]
    """

    def __init__(self, in_features: int = MAX_NEIGH,
                 num_classes: int = NUM_CLASSES):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(in_features, 64),
            nn.ReLU(),
            nn.Linear(64, 32),
            nn.ReLU(),
            nn.Linear(32, num_classes),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """
        Parameters
        ----------
        x : float tensor [N, MAX_NEIGH]

        Returns
        -------
        logits : float tensor [N, NUM_CLASSES]
        """
        return self.net(x)


# ===========================================================================
# Training loop
# ===========================================================================

def train(model: nn.Module,
          X_train: torch.Tensor, y_train: torch.Tensor,
          X_val:   torch.Tensor, y_val:   torch.Tensor) -> None:

    optimiser = torch.optim.Adam(model.parameters(), lr=LR)
    criterion = nn.CrossEntropyLoss()
    dataset   = torch.utils.data.TensorDataset(X_train, y_train)
    loader    = torch.utils.data.DataLoader(dataset,
                                            batch_size=BATCH_SIZE,
                                            shuffle=True)

    for epoch in range(1, EPOCHS + 1):
        model.train()
        total_loss = 0.0
        for xb, yb in loader:
            optimiser.zero_grad()
            loss = criterion(model(xb), yb)
            loss.backward()
            optimiser.step()
            total_loss += loss.item() * len(xb)

        if epoch % 10 == 0 or epoch == 1:
            model.eval()
            with torch.no_grad():
                val_preds = model(X_val).argmax(dim=1)
                val_acc = (val_preds == y_val).float().mean().item()
            avg_loss = total_loss / len(X_train)
            print(f"  Epoch {epoch:3d}/{EPOCHS}  "
                  f"loss={avg_loss:.4f}  val_acc={val_acc*100:.1f}%")


# ===========================================================================
# Entry point
# ===========================================================================

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", default="model_structure.pt",
                        help="Output TorchScript file (default: model_structure.pt)")
    args = parser.parse_args()

    output_path = Path(args.output)

    print("=" * 60)
    print("  OVITO ML Structure Classifier – Training")
    print("=" * 60)
    print(f"\nDescriptor : sorted {MAX_NEIGH} neighbor distances / {CUTOFF} Å")
    print(f"Configs    : {N_CONFIGS} per structure × 5 structures")
    print()

    # --- Build dataset ---
    print("Generating training data...")
    X, y = build_dataset(verbose=True)
    print(f"\nTotal samples: {len(X):,}")

    # Shuffle & split 80/20
    rng = np.random.default_rng(0)
    idx = rng.permutation(len(X))
    split = int(0.8 * len(idx))
    i_train, i_val = idx[:split], idx[split:]

    X_t = torch.from_numpy(X[i_train])
    y_t = torch.from_numpy(y[i_train])
    X_v = torch.from_numpy(X[i_val])
    y_v = torch.from_numpy(y[i_val])

    print(f"\nTrain: {len(X_t):,}   Val: {len(X_v):,}")

    # --- Train ---
    model = StructureClassifier()
    print(f"\nModel parameters: "
          f"{sum(p.numel() for p in model.parameters()):,}\n")
    print("Training...")
    train(model, X_t, y_t, X_v, y_v)

    # --- Final accuracy per class ---
    model.eval()
    with torch.no_grad():
        preds = model(X_v).argmax(dim=1)
    print("\nPer-class validation accuracy:")
    for cls_id, cls_name in enumerate(CLASS_NAMES):
        mask = y_v == cls_id
        if mask.sum() > 0:
            acc = (preds[mask] == cls_id).float().mean().item()
            print(f"  {cls_name:8s}: {acc*100:.1f}%  ({mask.sum()} samples)")

    # --- Export TorchScript ---
    # Use torch.jit.script() for full symbolic tracing (safe with control flow).
    scripted = torch.jit.script(model)
    scripted.save(str(output_path))
    print(f"\nTorchScript model saved → {output_path.resolve()}")
    print("\nTo use in OVITO:")
    print(f"  1. Add 'ML Structure Modifier' to your pipeline")
    print(f"  2. Set 'Model path' to: {output_path.resolve()}")
    print(f"  3. Set 'Cutoff radius' to {CUTOFF} Å")
    print(f"  4. The modifier writes an 'ML_Structure' per-particle property")
    print(f"     with values: 0=FCC  1=HCP  2=BCC  3=Diamond  4=SC")


if __name__ == "__main__":
    main()
