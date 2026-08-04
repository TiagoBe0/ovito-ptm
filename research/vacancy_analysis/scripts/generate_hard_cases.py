#!/usr/bin/env python3
"""Hard synthetic ground-truth cases beyond isotropic thermal noise.

Generates the "hard regime" case families used to test whether a vacancy
classifier trained on thermal noise generalizes, and to supply training
diversity for the ML mode (see ml_prototype_vacancy_classifier.py). All
families keep the simulation cell fixed and use zero-mean displacement fields,
so they exercise the modifier exactly as the sweep service runs it
(NO_MAPPING affine mode) and stay PBC-consistent.

Families (each a dataset directory compatible with cases_manifest.json):

  strain   Non-affine sinusoidal shear u_x(z) = A*sin(2*pi*z/Lz) on top of mild
           thermal noise (sigma = 0.06*nn). Sweep variable = A in nn units.
           Homogeneous-looking locally, but atoms shift across WS cell
           boundaries once A approaches ~0.5*nn.

  cluster  Di- and tri-vacancy clusters (12 mono + 8 di + 4 tri = 40 vacant
           sites) under the same isotropic noise sweep as the base dataset.
           Tests recall when true vacancies are spatially correlated - the
           same signature Frenkel recombination and candidate-clustering
           features tend to treat as artifacts.

  cascade  A spherical "cascade core" (radius core_radius) whose atoms get
           strong random displacements, mild noise elsewhere; 8 of the 40
           vacancies are forced inside the core, 32 outside. Sweep variable =
           core displacement sigma in nn units. Ground truth records which
           vacancies are in-core for stratified scoring.

The sweep variable is stored as "noise_sigma" in ground_truth.json so the
existing collection pipeline can treat every family uniformly.
"""

import argparse
import json
import math
import random
from pathlib import Path

from generate_noise_cases import (FCC_BASIS, make_fcc, min_image_dsq,
                                  pick_isolated_vacancies, write_extxyz)

DEFAULT_ROOT = Path(__file__).resolve().parents[1] / "datasets" / "generated"


def nn_neighbors(site, sites, cell, nn):
    """Site indices at first-neighbor distance of `site` (periodic)."""
    lo, hi = (0.9 * nn) ** 2, (1.1 * nn) ** 2
    p = site["pos"]
    return [i for i, s in enumerate(sites)
            if lo <= min_image_dsq(p, s["pos"], cell) <= hi]


def pick_vacancy_clusters(sites, cell, nn, rng, n_mono=12, n_di=8, n_tri=4):
    """Vacancy set made of isolated mono/di/tri clusters. Returns sorted ids."""
    seeds = pick_isolated_vacancies(sites, cell, n_mono + n_di + n_tri,
                                    min_sep=3.0 * nn, rng=rng)
    rng.shuffle(seeds)
    chosen = set()
    plan = ["mono"] * n_mono + ["di"] * n_di + ["tri"] * n_tri
    for seed_idx, kind in zip(seeds, plan):
        cluster = [seed_idx]
        if kind != "mono":
            neigh = [i for i in nn_neighbors(sites[seed_idx], sites, cell, nn)
                     if i not in chosen]
            rng.shuffle(neigh)
            cluster += neigh[:(1 if kind == "di" else 2)]
        chosen.update(cluster)
    return sorted(chosen)


def emit_case(out_dir, name, current, cell, gt):
    case_dir = out_dir / name
    case_dir.mkdir(parents=True, exist_ok=True)
    write_extxyz(case_dir / "current.xyz", current, cell)
    (case_dir / "ground_truth.json").write_text(json.dumps(gt, indent=2) + "\n")


def emit_manifest(out_dir, family, nx, a, nn, cell, n_sites, vac_set, sweep, seed, cases):
    (out_dir / "cases_manifest.json").write_text(json.dumps({
        "dataset": out_dir.name, "family": family,
        "structure": "fcc", "replication": [nx, nx, nx], "lattice_constant": a,
        "nn_distance": nn, "cell": list(cell),
        "reference_site_count": n_sites, "n_vacancies": len(vac_set),
        "vacancy_site_ids": vac_set, "sigmas": sweep, "seed": seed,
        "reference_file": "reference.xyz", "cases": cases,
    }, indent=2) + "\n")


def displaced(keep, rng_amp, cell):
    """Copy of `keep` with per-site displacement amp(site) applied."""
    out = []
    for s in keep:
        x, y, z = s["pos"]
        dx, dy, dz = rng_amp(s)
        out.append({"site_id": s["site_id"], "species": "Al",
                    "pos": (x + dx, y + dy, z + dz)})
    return out


def build_family(family, nx, a, seed, sweep, root):
    out_dir = root / f"{family}_fcc_{nx}x{nx}x{nx}{'' if seed == 42 else f'_seed{seed}'}"
    rng = random.Random(seed)
    cell = (nx * a, nx * a, nx * a)
    sites = make_fcc(nx, nx, nx, a)
    nn = a / math.sqrt(2.0)

    core_center = tuple(c / 2 for c in cell)
    core_radius = 6.0
    in_core = lambda p: min_image_dsq(p, core_center, cell) <= core_radius ** 2

    if family == "cluster":
        vac_set = pick_vacancy_clusters(sites, cell, nn, rng)
        extra_gt = {}
    elif family == "cascade":
        core_ids = [i for i, s in enumerate(sites) if in_core(s["pos"])]
        rng.shuffle(core_ids)
        vac_core = []
        for i in core_ids:  # 8 in-core vacancies, loosely separated
            if all(min_image_dsq(sites[i]["pos"], sites[c]["pos"], cell) >= (1.0 * nn) ** 2
                   for c in vac_core):
                vac_core.append(i)
                if len(vac_core) == 8:
                    break
        outside = [s for s in sites if not in_core(s["pos"])]
        vac_out = pick_isolated_vacancies(outside, cell, 32, min_sep=1.6 * nn, rng=rng)
        vac_out = [outside[i]["site_id"] for i in vac_out]
        vac_set = sorted(vac_core + vac_out)
        extra_gt = {"core_center": core_center, "core_radius": core_radius,
                    "in_core_vacancy_site_ids": sorted(vac_core)}
    else:  # strain
        vac_set = pick_isolated_vacancies(sites, cell, 40, min_sep=1.6 * nn, rng=rng)
        extra_gt = {}
    vac_lookup = set(vac_set)
    keep = [s for s in sites if s["site_id"] not in vac_lookup]

    out_dir.mkdir(parents=True, exist_ok=True)
    write_extxyz(out_dir / "reference.xyz", sites, cell)

    cases = []
    for level in sweep:
        drng = random.Random(seed * 1000 + int(round(level * 1000)))
        if family == "strain":
            amp, noise = level * nn, 0.06 * nn
            two_pi_over_l = 2.0 * math.pi / cell[2]

            def disp(s, amp=amp, noise=noise, k=two_pi_over_l):
                return (amp * math.sin(k * s["pos"][2]) + drng.gauss(0, noise),
                        drng.gauss(0, noise), drng.gauss(0, noise))
            extra_case = {"shear_amplitude_nn": level, "thermal_sigma": 0.06}
        elif family == "cascade":
            core_amp, noise = level * nn, 0.04 * nn

            def disp(s, core_amp=core_amp, noise=noise):
                a_ = core_amp if in_core(s["pos"]) else noise
                return (drng.gauss(0, a_), drng.gauss(0, a_), drng.gauss(0, a_))
            extra_case = {"core_sigma_nn": level, "thermal_sigma": 0.04}
        else:  # cluster: plain isotropic noise sweep
            amp = level * nn

            def disp(s, amp=amp):
                return (drng.gauss(0, amp), drng.gauss(0, amp), drng.gauss(0, amp))
            extra_case = {}

        name = f"level_{level:.2f}"
        emit_case(out_dir, name, displaced(keep, disp, cell), cell, {
            "case": name, "family": family, "noise_sigma": level,
            "nn_distance": nn, "n_vacancies": len(vac_set),
            "vacancy_site_ids": vac_set, **extra_gt, **extra_case,
        })
        cases.append(name)

    emit_manifest(out_dir, family, nx, a, nn, cell, len(sites), vac_set, sweep, seed, cases)
    print(f"wrote {len(cases)} {family} cases to {out_dir} "
          f"(sites={len(sites)}, vacancies={len(vac_set)})")


SWEEPS = {
    "strain": [0.2, 0.4, 0.6, 0.8, 1.0],
    "cluster": [0.04, 0.08, 0.12, 0.16, 0.20],
    "cascade": [0.15, 0.25, 0.35, 0.45, 0.60],
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--families", nargs="*", default=list(SWEEPS))
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--nx", type=int, default=8)
    ap.add_argument("--lattice-constant", type=float, default=3.6)
    ap.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    args = ap.parse_args()
    for family in args.families:
        build_family(family, args.nx, args.lattice_constant, args.seed,
                     SWEEPS[family], args.root)


if __name__ == "__main__":
    main()
