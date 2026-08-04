#!/usr/bin/env python3
"""Validate synthetic vacancy-analysis cases with a small WS oracle."""

from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path


DEFECT_TYPE = {
    0: "OK",
    1: "Vacancy",
    2: "Interstitial",
    3: "Frenkel pair",
}


def parse_lattice(comment: str):
    match = re.search(r'Lattice="([^"]+)"', comment)
    if not match:
        raise ValueError("Missing extended XYZ Lattice metadata")
    values = [float(x) for x in match.group(1).split()]
    if len(values) != 9:
        raise ValueError("Expected 9 Lattice values")
    return (values[0], values[4], values[8])


def read_extxyz(path: Path):
    with path.open("r", encoding="utf-8") as f:
        lines = [line.rstrip("\n") for line in f]
    count = int(lines[0])
    cell = parse_lattice(lines[1])
    atoms = []
    for line in lines[2:]:
        if not line.strip():
            continue
        species, x, y, z, site_id = line.split()[:5]
        atoms.append(
            {
                "species": species,
                "pos": (float(x), float(y), float(z)),
                "site_id": int(site_id),
            }
        )
    if len(atoms) != count:
        raise ValueError(f"{path}: header count {count} does not match {len(atoms)} atoms")
    return cell, atoms


def min_image_delta(a, b, cell):
    delta = [a[i] - b[i] for i in range(3)]
    for i in range(3):
        delta[i] -= round(delta[i] / cell[i]) * cell[i]
    return tuple(delta)


def distance_sq(a, b, cell):
    d = min_image_delta(a, b, cell)
    return d[0] * d[0] + d[1] * d[1] + d[2] * d[2]


def nearest_site(point, reference_atoms, cell):
    best_index = None
    best_dist = float("inf")
    for i, atom in enumerate(reference_atoms):
        d = distance_sq(point, atom["pos"], cell)
        if d < best_dist:
            best_index = i
            best_dist = d
    return best_index, best_dist


def estimate_nn(reference_atoms, cell):
    best = float("inf")
    for i, atom in enumerate(reference_atoms):
        for j, other in enumerate(reference_atoms):
            if i == j:
                continue
            d = distance_sq(atom["pos"], other["pos"], cell)
            if 0 < d < best:
                best = d
    return math.sqrt(best)


def compute_counts(reference_atoms, current_atoms, cell):
    occupancy = [0 for _ in reference_atoms]
    for atom in current_atoms:
        site, _ = nearest_site(atom["pos"], reference_atoms, cell)
        occupancy[site] += 1

    vacant_sites = [i for i, occ in enumerate(occupancy) if occ == 0]
    interstitial_surplus = [max(0, occ - 1) for occ in occupancy]
    raw_ws_vacancies = len(vacant_sites)
    raw_ws_interstitials = sum(interstitial_surplus)

    r_nn = estimate_nn(reference_atoms, cell)
    cutoff = 1.2 * r_nn
    candidates = []
    for vacant in vacant_sites:
        for site, surplus in enumerate(interstitial_surplus):
            if surplus <= 0:
                continue
            d = math.sqrt(distance_sq(reference_atoms[vacant]["pos"], reference_atoms[site]["pos"], cell))
            if d <= cutoff:
                candidates.append((d, vacant, site))

    matched_vacancies = set()
    frenkel_recombined = 0
    for _, vacant, site in sorted(candidates):
        if vacant in matched_vacancies or interstitial_surplus[site] <= 0:
            continue
        matched_vacancies.add(vacant)
        interstitial_surplus[site] -= 1
        frenkel_recombined += 1

    defect_type = [0 for _ in reference_atoms]
    for site, surplus in enumerate(interstitial_surplus):
        if surplus > 0:
            defect_type[site] = 2
    for vacant in vacant_sites:
        defect_type[vacant] = 3 if vacant in matched_vacancies else 1

    return {
        "raw_ws_vacancies": raw_ws_vacancies,
        "raw_ws_interstitials": raw_ws_interstitials,
        "frenkel_recombined": frenkel_recombined,
        "vacancy_count": raw_ws_vacancies - frenkel_recombined,
        "interstitial_count": raw_ws_interstitials - frenkel_recombined,
        "occupancy": occupancy,
        "corrected_interstitial_surplus": interstitial_surplus,
        "defect_type": defect_type,
    }


def validate_case(case_dir: Path):
    ref_cell, reference_atoms = read_extxyz(case_dir / "reference.xyz")
    cur_cell, current_atoms = read_extxyz(case_dir / "current.xyz")
    if ref_cell != cur_cell:
        raise AssertionError(f"{case_dir.name}: reference/current cells differ")

    with (case_dir / "ground_truth.json").open("r", encoding="utf-8") as f:
        ground_truth = json.load(f)

    actual = compute_counts(reference_atoms, current_atoms, ref_cell)
    expected_attrs = ground_truth["expected_attributes"]
    for key, expected in expected_attrs.items():
        if actual[key] != expected:
            raise AssertionError(f"{case_dir.name}: {key} expected {expected}, got {actual[key]}")

    for site_id, expected in ground_truth["expected_sites"].items():
        site = int(site_id)
        if actual["occupancy"][site] != expected["raw_occupancy"]:
            raise AssertionError(
                f"{case_dir.name}: site {site} raw occupancy expected "
                f"{expected['raw_occupancy']}, got {actual['occupancy'][site]}"
            )
        surplus = actual["corrected_interstitial_surplus"][site]
        if surplus != expected["corrected_interstitial_surplus"]:
            raise AssertionError(
                f"{case_dir.name}: site {site} corrected surplus expected "
                f"{expected['corrected_interstitial_surplus']}, got {surplus}"
            )
        actual_type = DEFECT_TYPE[actual["defect_type"][site]]
        if actual_type != expected["expected_defect_type"]:
            raise AssertionError(
                f"{case_dir.name}: site {site} defect type expected "
                f"{expected['expected_defect_type']}, got {actual_type}"
            )

    return actual


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dataset", type=Path)
    return parser.parse_args()


def main():
    args = parse_args()
    manifest_path = args.dataset / "cases_manifest.json"
    with manifest_path.open("r", encoding="utf-8") as f:
        manifest = json.load(f)

    for case in manifest["cases"]:
        actual = validate_case(args.dataset / case)
        print(
            f"{case}: OK "
            f"(raw V/I={actual['raw_ws_vacancies']}/{actual['raw_ws_interstitials']}, "
            f"corrected V/I={actual['vacancy_count']}/{actual['interstitial_count']}, "
            f"Frenkel={actual['frenkel_recombined']})"
        )


if __name__ == "__main__":
    main()
