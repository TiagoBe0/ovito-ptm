#!/usr/bin/env python3
"""Generate synthetic FCC cases with known vacancies under increasing thermal noise.

Builds a ground-truth population for a precision/recall validation of the
VacancyAnalysisModifier (CONTEXTO_vacancy.md: "curva precision/recall vs
temperatura o ruido"). A single ordered FCC crystal is used as the reference;
the current configuration removes a fixed set of Nv isolated vacancies and then
displaces every remaining atom by an isotropic Gaussian of amplitude
sigma * nn_distance. One case directory per noise level, all sharing the same
vacancy set so recall is comparable across levels.

Ground truth per case: the set of reference site_ids that are genuine vacancies.
Everything else should be classified OK (any other reported vacancy is a false
positive).
"""

import argparse
import json
import math
import random
from pathlib import Path

FCC_BASIS = ((0.0, 0.0, 0.0), (0.0, 0.5, 0.5), (0.5, 0.0, 0.5), (0.5, 0.5, 0.0))


def make_fcc(nx, ny, nz, a):
    sites = []
    sid = 0
    for ix in range(nx):
        for iy in range(ny):
            for iz in range(nz):
                for bx, by, bz in FCC_BASIS:
                    sites.append({"site_id": sid, "species": "Al",
                                  "pos": ((ix + bx) * a, (iy + by) * a, (iz + bz) * a)})
                    sid += 1
    return sites


def min_image_dsq(a, b, cell):
    s = 0.0
    for i in range(3):
        d = a[i] - b[i]
        d -= round(d / cell[i]) * cell[i]
        s += d * d
    return s


def pick_isolated_vacancies(sites, cell, n_vac, min_sep, rng):
    """Pick n_vac site indices no closer than min_sep to each other."""
    order = list(range(len(sites)))
    rng.shuffle(order)
    chosen = []
    min_sep_sq = min_sep * min_sep
    for idx in order:
        p = sites[idx]["pos"]
        if all(min_image_dsq(p, sites[c]["pos"], cell) >= min_sep_sq for c in chosen):
            chosen.append(idx)
            if len(chosen) == n_vac:
                break
    if len(chosen) < n_vac:
        raise SystemExit(f"could only place {len(chosen)}/{n_vac} isolated vacancies; "
                         f"reduce --n-vacancies or enlarge the box")
    return sorted(chosen)


def write_extxyz(path, atoms, cell):
    lattice = f'{cell[0]:.10f} 0 0 0 {cell[1]:.10f} 0 0 0 {cell[2]:.10f}'
    header = (f'Lattice="{lattice}" '
              'Properties=species:S:1:pos:R:3:site_id:I:1 pbc="T T T"')
    with path.open("w", encoding="utf-8") as f:
        f.write(f"{len(atoms)}\n{header}\n")
        for at in atoms:
            x, y, z = at["pos"]
            f.write(f'{at["species"]} {x:.10f} {y:.10f} {z:.10f} {at["site_id"]}\n')


def build(nx, ny, nz, a, n_vac, sigmas, seed, output):
    rng = random.Random(seed)
    cell = (nx * a, ny * a, nz * a)
    sites = make_fcc(nx, ny, nz, a)
    nn = a / math.sqrt(2.0)                      # FCC first-neighbor distance
    vac_set = pick_isolated_vacancies(sites, cell, n_vac, min_sep=1.6 * nn, rng=rng)
    vac_lookup = set(vac_set)
    keep = [s for s in sites if s["site_id"] not in vac_lookup]

    output.mkdir(parents=True, exist_ok=True)
    write_extxyz(output / "reference.xyz", sites, cell)   # shared pristine reference

    case_names = []
    for sigma in sigmas:
        amp = sigma * nn
        # Independent RNG per level (seeded) so displacements are reproducible
        # and the vacancy set is identical across levels.
        drng = random.Random(seed * 1000 + int(round(sigma * 1000)))
        current = []
        for s in keep:
            x, y, z = s["pos"]
            current.append({"site_id": s["site_id"], "species": "Al",
                            "pos": (x + drng.gauss(0, amp),
                                    y + drng.gauss(0, amp),
                                    z + drng.gauss(0, amp))})
        name = f"sigma_{sigma:.2f}"
        case_dir = output / name
        case_dir.mkdir(parents=True, exist_ok=True)
        write_extxyz(case_dir / "current.xyz", current, cell)
        (case_dir / "ground_truth.json").write_text(json.dumps({
            "case": name, "noise_sigma": sigma, "noise_amplitude": amp,
            "nn_distance": nn, "n_vacancies": n_vac,
            "vacancy_site_ids": vac_set,
        }, indent=2) + "\n")
        case_names.append(name)

    (output / "cases_manifest.json").write_text(json.dumps({
        "dataset": output.name,
        "structure": "fcc", "replication": [nx, ny, nz], "lattice_constant": a,
        "nn_distance": nn, "cell": list(cell),
        "reference_site_count": len(sites), "n_vacancies": n_vac,
        "vacancy_site_ids": vac_set, "sigmas": sigmas, "seed": seed,
        "reference_file": "reference.xyz", "cases": case_names,
    }, indent=2) + "\n")
    print(f"wrote {len(case_names)} noise cases to {output} "
          f"(sites={len(sites)}, vacancies={n_vac}, nn={nn:.3f})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nx", type=int, default=8)
    ap.add_argument("--ny", type=int, default=8)
    ap.add_argument("--nz", type=int, default=8)
    ap.add_argument("--lattice-constant", type=float, default=3.6)
    ap.add_argument("--n-vacancies", type=int, default=40)
    ap.add_argument("--sigmas", type=float, nargs="*",
                    default=[round(0.02 * i, 2) for i in range(0, 11)])
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--output", type=Path,
                    default=Path("research/vacancy_analysis/datasets/generated/noise_fcc_8x8x8"))
    args = ap.parse_args()
    build(args.nx, args.ny, args.nz, args.lattice_constant,
          args.n_vacancies, args.sigmas, args.seed, args.output)


if __name__ == "__main__":
    main()
