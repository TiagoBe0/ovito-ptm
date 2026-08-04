#!/usr/bin/env python3
"""Generate small synthetic vacancy-analysis cases with ground truth."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


FCC_BASIS = (
    (0.0, 0.0, 0.0),
    (0.0, 0.5, 0.5),
    (0.5, 0.0, 0.5),
    (0.5, 0.5, 0.0),
)


def vec_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vec_add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def vec_scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dist_sq(a, b):
    d = vec_sub(a, b)
    return d[0] * d[0] + d[1] * d[1] + d[2] * d[2]


def make_fcc(nx: int, ny: int, nz: int, a: float):
    sites = []
    site_id = 0
    for ix in range(nx):
        for iy in range(ny):
            for iz in range(nz):
                for bx, by, bz in FCC_BASIS:
                    pos = ((ix + bx) * a, (iy + by) * a, (iz + bz) * a)
                    sites.append({"site_id": site_id, "species": "Al", "pos": pos})
                    site_id += 1
    return sites


def find_central_site(sites, cell):
    center = (cell[0] * 0.5, cell[1] * 0.5, cell[2] * 0.5)
    return min(range(len(sites)), key=lambda i: dist_sq(sites[i]["pos"], center))


def find_nearest_site(sites, site_index):
    origin = sites[site_index]["pos"]
    candidates = [i for i in range(len(sites)) if i != site_index]
    return min(candidates, key=lambda i: dist_sq(sites[i]["pos"], origin))


def write_extxyz(path: Path, atoms, cell):
    lattice = f'{cell[0]:.10f} 0 0 0 {cell[1]:.10f} 0 0 0 {cell[2]:.10f}'
    header = (
        f'Lattice="{lattice}" '
        'Properties=species:S:1:pos:R:3:site_id:I:1 '
        'pbc="T T T"'
    )
    with path.open("w", encoding="utf-8") as f:
        f.write(f"{len(atoms)}\n")
        f.write(f"{header}\n")
        for atom in atoms:
            x, y, z = atom["pos"]
            f.write(f'{atom["species"]} {x:.10f} {y:.10f} {z:.10f} {atom["site_id"]}\n')


def write_case(root: Path, name: str, reference_sites, current_atoms, cell, metadata, expected):
    case_dir = root / name
    case_dir.mkdir(parents=True, exist_ok=True)
    write_extxyz(case_dir / "reference.xyz", reference_sites, cell)
    write_extxyz(case_dir / "current.xyz", current_atoms, cell)

    ground_truth = {
        "case": name,
        "description": expected["description"],
        "lattice": metadata,
        "expected_attributes": expected["attributes"],
        "expected_sites": expected["sites"],
    }
    with (case_dir / "ground_truth.json").open("w", encoding="utf-8") as f:
        json.dump(ground_truth, f, indent=2, sort_keys=True)
        f.write("\n")


def build_cases(nx: int, ny: int, nz: int, a: float, output: Path):
    cell = (nx * a, ny * a, nz * a)
    sites = make_fcc(nx, ny, nz, a)
    vacancy_site = find_central_site(sites, cell)
    interstitial_site = find_nearest_site(sites, vacancy_site)
    vacancy_pos = sites[vacancy_site]["pos"]
    interstitial_pos = sites[interstitial_site]["pos"]
    nearest_neighbor_distance = math.sqrt(dist_sq(vacancy_pos, interstitial_pos))

    direction = vec_sub(interstitial_pos, vacancy_pos)
    length = math.sqrt(sum(c * c for c in direction))
    unit = tuple(c / length for c in direction)
    extra_pos = vec_add(interstitial_pos, vec_scale(unit, 0.15 * nearest_neighbor_distance))

    metadata = {
        "structure": "fcc",
        "replication": [nx, ny, nz],
        "lattice_constant": a,
        "cell": list(cell),
        "reference_site_count": len(sites),
        "vacancy_site_id": vacancy_site,
        "interstitial_site_id": interstitial_site,
        "nearest_neighbor_distance": nearest_neighbor_distance,
        "auto_recombination_cutoff": 1.2 * nearest_neighbor_distance,
    }

    perfect_atoms = [dict(atom) for atom in sites]
    vacancy_atoms = [dict(atom) for atom in sites if atom["site_id"] != vacancy_site]
    frenkel_atoms = [dict(atom) for atom in vacancy_atoms]
    frenkel_atoms.append({"site_id": -1, "species": "Al", "pos": extra_pos})

    output.mkdir(parents=True, exist_ok=True)

    write_case(
        output,
        "perfect",
        sites,
        perfect_atoms,
        cell,
        metadata,
        {
            "description": "Perfect FCC crystal; no point defects expected.",
            "attributes": {
                "raw_ws_vacancies": 0,
                "raw_ws_interstitials": 0,
                "frenkel_recombined": 0,
                "vacancy_count": 0,
                "interstitial_count": 0,
            },
            "sites": {},
        },
    )

    write_case(
        output,
        "single_vacancy",
        sites,
        vacancy_atoms,
        cell,
        metadata,
        {
            "description": "One atom removed from a central FCC site.",
            "attributes": {
                "raw_ws_vacancies": 1,
                "raw_ws_interstitials": 0,
                "frenkel_recombined": 0,
                "vacancy_count": 1,
                "interstitial_count": 0,
            },
            "sites": {
                str(vacancy_site): {
                    "expected_defect_type": "Vacancy",
                    "raw_occupancy": 0,
                    "corrected_interstitial_surplus": 0,
                }
            },
        },
    )

    write_case(
        output,
        "near_frenkel_pair",
        sites,
        frenkel_atoms,
        cell,
        metadata,
        {
            "description": "One central atom removed and one extra atom placed near an adjacent site.",
            "attributes": {
                "raw_ws_vacancies": 1,
                "raw_ws_interstitials": 1,
                "frenkel_recombined": 1,
                "vacancy_count": 0,
                "interstitial_count": 0,
            },
            "sites": {
                str(vacancy_site): {
                    "expected_defect_type": "Frenkel pair",
                    "raw_occupancy": 0,
                    "corrected_interstitial_surplus": 0,
                },
                str(interstitial_site): {
                    "expected_defect_type": "OK",
                    "raw_occupancy": 2,
                    "corrected_interstitial_surplus": 0,
                },
            },
        },
    )

    manifest = {
        "dataset": output.name,
        "metadata": metadata,
        "cases": ["perfect", "single_vacancy", "near_frenkel_pair"],
    }
    with (output / "cases_manifest.json").open("w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
        f.write("\n")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nx", type=int, default=4)
    parser.add_argument("--ny", type=int, default=4)
    parser.add_argument("--nz", type=int, default=4)
    parser.add_argument("--lattice-constant", type=float, default=3.6)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("research/vacancy_analysis/datasets/generated/fcc_4x4x4"),
    )
    return parser.parse_args()


def main():
    args = parse_args()
    if min(args.nx, args.ny, args.nz) < 3:
        raise SystemExit("Use at least 3 cells in each direction so defects are away from boundaries.")
    build_cases(args.nx, args.ny, args.nz, args.lattice_constant, args.output)
    print(f"Wrote synthetic cases to {args.output}")


if __name__ == "__main__":
    main()
