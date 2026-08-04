#!/usr/bin/env python3
"""Aggregate the per-energy vacancy sweep CSVs into manuscript-ready tables.

Implements task #3 from CONTEXTO_testing_database.md: per-energy summary with
max, final value, temporal area-under-curve, and the stage fractions
(Frenkel / structural / survivor).

Reads research/vacancy_analysis/results/energy_sweep/<energy>_masked_grid.csv
and writes:
  - energy_sweep_summary.csv : one row per (energy, mask_signal, mask_threshold)
  - energy_sweep_timeseries.csv : tidy long-form per-frame rows for plotting

Derived metrics (per the context; 0/0 defined as 0):
  survivor_fraction   = vacancy_count / raw_ws_vacancies
  frenkel_fraction    = frenkel_recombined / raw_ws_vacancies
  structural_fraction = structural_excluded / raw_ws_vacancies
  corrected_reduction = 1 - vacancy_count / raw_ws_vacancies
"""

import csv
import glob
import os

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SWEEP_DIR = os.path.join(REPO_ROOT, "research", "vacancy_analysis", "results", "energy_sweep")
OUT_SUMMARY = os.path.join(SWEEP_DIR, "energy_sweep_summary.csv")
OUT_TIMESERIES = os.path.join(SWEEP_DIR, "energy_sweep_timeseries.csv")


def safe_div(a, b):
    return a / b if b else 0.0


def trapz_auc(xs, ys):
    """Area under y(x) by the trapezoidal rule; needs >=2 points."""
    pts = sorted(zip(xs, ys))
    area = 0.0
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        area += 0.5 * (y1 + y0) * (x1 - x0)
    return area


def load_rows(path, energy):
    rows = []
    skipped = 0
    with open(path) as fh:
        for r in csv.DictReader(fh):
            if r.get("status_type") != "success":
                skipped += 1
                continue
            rows.append({
                "energy": energy,
                "timestep": int(r["timestep"]),
                "mask_signal": r["mask_signal"],
                "mask_threshold": float(r["mask_threshold"]),
                "raw_ws_vacancies": int(r["raw_ws_vacancies"]),
                "vacancy_count": int(r["vacancy_count"]),
                "frenkel_recombined": int(r["frenkel_recombined"]),
                "structural_excluded": int(r["structural_excluded"]),
                "surface_or_void_excluded": int(r["surface_or_void_excluded"]),
                "freevolume_excluded": int(r["freevolume_excluded"]),
            })
    if skipped:
        print(f"  warning: {energy}: skipped {skipped} non-success rows "
              f"(e.g. missing mask signal -> status=warning)")
    return rows


def main():
    files = sorted(glob.glob(os.path.join(SWEEP_DIR, "*_masked_grid.csv")))
    if not files:
        raise SystemExit(f"no *_masked_grid.csv found in {SWEEP_DIR}; run the sweep first")

    all_rows = []
    for path in files:
        energy = os.path.basename(path).replace("_masked_grid.csv", "")
        all_rows.extend(load_rows(path, energy))

    # tidy time series with derived per-frame fractions
    ts_cols = ["energy", "mask_signal", "mask_threshold", "timestep",
               "raw_ws_vacancies", "vacancy_count", "frenkel_recombined",
               "freevolume_excluded", "structural_excluded", "surface_or_void_excluded",
               "survivor_fraction", "frenkel_fraction", "structural_fraction",
               "corrected_reduction"]
    with open(OUT_TIMESERIES, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=ts_cols)
        w.writeheader()
        for r in all_rows:
            raw = r["raw_ws_vacancies"]
            w.writerow({
                **{k: r[k] for k in ts_cols if k in r},
                "survivor_fraction": round(safe_div(r["vacancy_count"], raw), 4),
                "frenkel_fraction": round(safe_div(r["frenkel_recombined"], raw), 4),
                "structural_fraction": round(safe_div(r["structural_excluded"], raw), 4),
                "corrected_reduction": round(1 - safe_div(r["vacancy_count"], raw), 4),
            })

    # group by (energy, mask_signal, mask_threshold) -> summary
    groups = {}
    for r in all_rows:
        key = (r["energy"], r["mask_signal"], r["mask_threshold"])
        groups.setdefault(key, []).append(r)

    sum_cols = ["energy", "mask_signal", "mask_threshold", "n_frames",
                "max_raw_ws_vacancies", "max_vacancy_count",
                "final_raw_ws_vacancies", "final_vacancy_count",
                "auc_raw_ws_vacancies", "auc_vacancy_count",
                "reduction_at_peak", "defect_weighted_reduction",
                "mean_survivor_fraction", "mean_frenkel_fraction",
                "mean_structural_fraction", "mean_corrected_reduction"]
    with open(OUT_SUMMARY, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=sum_cols)
        w.writeheader()
        for key in sorted(groups):
            energy, sig, thr = key
            g = sorted(groups[key], key=lambda r: r["timestep"])
            ts = [r["timestep"] for r in g]
            raw = [r["raw_ws_vacancies"] for r in g]
            vac = [r["vacancy_count"] for r in g]
            surv = [safe_div(a, b) for a, b in zip(vac, raw)]
            fren = [safe_div(r["frenkel_recombined"], r["raw_ws_vacancies"]) for r in g]
            stru = [safe_div(r["structural_excluded"], r["raw_ws_vacancies"]) for r in g]
            n = len(g)
            # Honest aggregate reductions. The per-frame mean is biased low because
            # most frames are near-equilibrium (raw ~ 0); report instead the
            # reduction at the peak-damage frame and the defect-count-weighted
            # reduction (1 - sum(vac)/sum(raw)).
            peak_i = max(range(n), key=lambda i: raw[i])
            reduction_at_peak = 1 - safe_div(vac[peak_i], raw[peak_i])
            defect_weighted_reduction = 1 - safe_div(sum(vac), sum(raw))
            w.writerow({
                "energy": energy, "mask_signal": sig, "mask_threshold": thr,
                "n_frames": n,
                "max_raw_ws_vacancies": max(raw), "max_vacancy_count": max(vac),
                "final_raw_ws_vacancies": raw[-1], "final_vacancy_count": vac[-1],
                "auc_raw_ws_vacancies": round(trapz_auc(ts, raw), 2) if n > 1 else 0,
                "auc_vacancy_count": round(trapz_auc(ts, vac), 2) if n > 1 else 0,
                "reduction_at_peak": round(reduction_at_peak, 4),
                "defect_weighted_reduction": round(defect_weighted_reduction, 4),
                "mean_survivor_fraction": round(sum(surv) / n, 4),
                "mean_frenkel_fraction": round(sum(fren) / n, 4),
                "mean_structural_fraction": round(sum(stru) / n, 4),
                "mean_corrected_reduction": round(1 - sum(surv) / n, 4),
            })

    print(f"wrote {OUT_SUMMARY}")
    print(f"wrote {OUT_TIMESERIES}")
    print(f"groups: {len(groups)}  frames-total: {len(all_rows)}")


if __name__ == "__main__":
    main()
