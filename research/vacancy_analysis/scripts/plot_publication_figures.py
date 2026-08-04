#!/usr/bin/env python3
"""Publication figures for the FeCrNi vacancy-analysis sweep.

Implements the figure set of CONTEXTO_testing_database.md. Every figure is an
independent file (no multi-panel grids) and carries NO title -- only axis
labels and legends, as required for manuscript composition.

Inputs (produced by run_energy_sweep.py + summarize_energy_sweep.py):
  results/energy_sweep/energy_sweep_summary.csv
  results/energy_sweep/energy_sweep_timeseries.csv
  results/energy_sweep/<energy>_nomask.csv       (per-energy no-mask baseline)
  results/energy_sweep/sensitivity/<energy>_cutoff.csv   (optional, fig 6)

Outputs: results/figures/*.pdf and *.png (300 dpi). One file per figure.

Figures:
  1. fig_energy_vs_raw_ws        : PKA energy vs peak raw Wigner-Seitz vacancies
  2. fig_energy_vs_vacancy_ccoord: PKA energy vs peak filtered vacancy_count,
                                    one line per c_coord threshold (0.3/0.5/0.7)
  3. fig_reduction_vs_energy     : PKA energy vs mean corrected reduction
  4. fig_temporal_<energy>       : time evolution (raw WS / Frenkel-corrected
                                   no-mask / c_coord 0.5 / c_coord 0.7)
  5. fig_stage_fractions_<energy>: stacked stage budget over time for c_coord 0.5
  6. fig_cutoff_sensitivity      : vacancy_count vs Frenkel cutoff (if data present)
"""

import argparse
import csv
import os
import re
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SWEEP_DIR = os.path.join(REPO_ROOT, "research", "vacancy_analysis", "results", "energy_sweep")
SYNTH_DIR = os.path.join(REPO_ROOT, "research", "vacancy_analysis", "results", "synthetic")
FIG_DIR = os.path.join(REPO_ROOT, "research", "vacancy_analysis", "results", "figures")

PREDICTOR_STYLE = {"raw WS": ("#b02418", "o"), "method": ("#1f4eb0", "s")}

CCOORD = "lammps-c-coord"
CCOORD_THRESHOLDS = [0.3, 0.5, 0.7]

# Runs excluded from the manuscript: 2kev/3kev have a post-damage reference frame
# (no pristine timestep-0 dump, peak raw WS is 0/4 -> degenerate), and 6kev_it1 has
# a single usable frame. See CONTEXTO_testing_database.md.
EXCLUDED_ENERGIES = {"2kev", "3kev", "6kev_it1"}

# Publication-oriented rcParams: single-column width, vector-friendly, no title.
plt.rcParams.update({
    "figure.figsize": (3.5, 2.7),
    "font.size": 9,
    "axes.labelsize": 9,
    "legend.fontsize": 7.5,
    "xtick.labelsize": 8,
    "ytick.labelsize": 8,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "lines.linewidth": 1.4,
    "lines.markersize": 4,
    "legend.frameon": False,
    "savefig.bbox": "tight",
    "savefig.dpi": 300,
    "pdf.fonttype": 42,   # editable text in vector PDFs
    "ps.fonttype": 42,
})


def energy_to_kev(name):
    """Map a run label ('5kev', '8kev_real', '6kev_it2') to its keV value."""
    m = re.match(r"(\d+)kev", name)
    return float(m.group(1)) if m else np.nan


def save(fig, stem):
    os.makedirs(FIG_DIR, exist_ok=True)
    for ext in ("pdf", "png"):
        fig.savefig(os.path.join(FIG_DIR, f"{stem}.{ext}"))
    plt.close(fig)
    print(f"  wrote {stem}.pdf / .png")


def load_summary():
    path = os.path.join(SWEEP_DIR, "energy_sweep_summary.csv")
    if not os.path.exists(path):
        raise SystemExit(f"missing {path}; run summarize_energy_sweep.py first")
    rows = [r for r in read_csv_rows(path) if r["energy"] not in EXCLUDED_ENERGIES]
    for r in rows:
        r["kev"] = energy_to_kev(r["energy"])
    return rows


def load_timeseries():
    path = os.path.join(SWEEP_DIR, "energy_sweep_timeseries.csv")
    if not os.path.exists(path):
        raise SystemExit(f"missing {path}; run summarize_energy_sweep.py first")
    return [r for r in read_csv_rows(path) if r["energy"] not in EXCLUDED_ENERGIES]


def load_nomask_summary():
    """Per-energy Frenkel-only reduction from the <energy>_nomask.csv baselines."""
    import glob
    rows = []
    for path in sorted(glob.glob(os.path.join(SWEEP_DIR, "*_nomask.csv"))):
        energy = os.path.basename(path).replace("_nomask.csv", "")
        if energy in EXCLUDED_ENERGIES:
            continue
        recs = [r for r in read_csv_rows(path) if r["status_type"] == "success"]
        raw_sum = sum(r["raw_ws_vacancies"] for r in recs)
        vac_sum = sum(r["vacancy_count"] for r in recs)
        if raw_sum <= 0:
            continue
        rows.append({
            "energy": energy,
            "kev": energy_to_kev(energy),
            "defect_weighted_reduction": 1 - vac_sum / raw_sum,
        })
    return rows


def read_csv_rows(path):
    with open(path, newline="") as fh:
        rows = list(csv.DictReader(fh))
    for r in rows:
        for key, value in list(r.items()):
            if key in {"energy", "mask_signal"}:
                continue
            try:
                if value == "":
                    continue
                number = float(value)
            except ValueError:
                continue
            r[key] = int(number) if number.is_integer() else number
    return rows


def filter_rows(rows, **criteria):
    out = []
    for r in rows:
        ok = True
        for key, value in criteria.items():
            current = r.get(key)
            if isinstance(value, float):
                ok = np.isclose(current, value)
            else:
                ok = current == value
            if not ok:
                break
        if ok:
            out.append(r)
    return out


def group_rows(rows, keys):
    groups = defaultdict(list)
    for r in rows:
        groups[tuple(r[k] for k in keys)].append(r)
    return groups


def first_per_group(rows, group_keys, value_col):
    out = []
    for key, g in group_rows(rows, group_keys).items():
        r = dict(zip(group_keys, key))
        r[value_col] = g[0][value_col]
        out.append(r)
    return out


def agg_by_kev(df, value_col):
    """Mean +/- spread of value_col per keV (replicate 6keV runs collapse)."""
    rows = []
    for (kev,), g in group_rows(df, ["kev"]).items():
        values = [r[value_col] for r in g]
        rows.append({
            "kev": kev,
            "mean": sum(values) / len(values),
            "lo": min(values),
            "hi": max(values),
        })
    return sorted(rows, key=lambda r: r["kev"])


# ---------------------------------------------------------------------------
# Figure 1: PKA energy vs peak raw Wigner-Seitz vacancies.
# ---------------------------------------------------------------------------
def fig_energy_vs_raw_ws(summary):
    # max_raw_ws_vacancies is identical across mask configs of a given energy.
    per_energy = first_per_group(summary, ["energy", "kev"], "max_raw_ws_vacancies")
    a = agg_by_kev(per_energy, "max_raw_ws_vacancies")
    fig, ax = plt.subplots()
    x = [r["kev"] for r in a]
    y = [r["mean"] for r in a]
    yerr = np.vstack([[r["mean"] - r["lo"] for r in a], [r["hi"] - r["mean"] for r in a]])
    ax.errorbar(x, y, yerr=yerr, marker="o", capsize=2, color="#b02418")
    ax.set_xlabel("PKA energy (keV)")
    ax.set_ylabel("Peak raw WS vacancies")
    ax.set_ylim(bottom=0)
    save(fig, "fig_energy_vs_raw_ws")


# ---------------------------------------------------------------------------
# Figure 2: PKA energy vs peak filtered vacancy_count, per c_coord threshold.
# ---------------------------------------------------------------------------
def fig_energy_vs_vacancy_ccoord(summary):
    fig, ax = plt.subplots()
    colors = ["#1f4eb0", "#2a8a3e", "#d08a10"]
    for thr, color in zip(CCOORD_THRESHOLDS, colors):
        sub = filter_rows(summary, mask_signal=CCOORD, mask_threshold=thr)
        if not sub:
            continue
        a = agg_by_kev(sub, "max_vacancy_count")
        x = [r["kev"] for r in a]
        y = [r["mean"] for r in a]
        yerr = np.vstack([[r["mean"] - r["lo"] for r in a], [r["hi"] - r["mean"] for r in a]])
        ax.errorbar(x, y, yerr=yerr, marker="o", capsize=2,
                    color=color, label=f"c_coord thr {thr:g}")
    ax.set_xlabel("PKA energy (keV)")
    ax.set_ylabel("Peak filtered vacancies")
    ax.set_ylim(bottom=0)
    ax.legend()
    save(fig, "fig_energy_vs_vacancy_ccoord")


# ---------------------------------------------------------------------------
# Figure 3: PKA energy vs defect-weighted reduction vs raw WS.
# Frenkel-only (no mask) is the defensible headline; c_coord thresholds are shown
# as a lighter sensitivity band on top. Uses the defect-count-weighted reduction
# (1 - sum(vac)/sum(raw)), not the frame-averaged mean which is biased by the many
# near-equilibrium frames.
# ---------------------------------------------------------------------------
def fig_reduction_vs_energy(summary, nomask_summary):
    fig, ax = plt.subplots()

    # Headline: Frenkel-only reduction (structural mask disabled).
    a = agg_by_kev(nomask_summary, "defect_weighted_reduction")
    ax.plot([r["kev"] for r in a], [r["mean"] for r in a], marker="o",
            color="#444444", label="Frenkel only (no mask)")

    # Sensitivity: c_coord structural mask, thresholds 0.3 (most aggressive) to 0.7.
    colors = ["#1f4eb0", "#2a8a3e", "#d08a10"]
    for thr, color in zip(CCOORD_THRESHOLDS, colors):
        sub = filter_rows(summary, mask_signal=CCOORD, mask_threshold=thr)
        if not sub:
            continue
        a = agg_by_kev(sub, "defect_weighted_reduction")
        ax.plot([r["kev"] for r in a], [r["mean"] for r in a],
                marker="^", color=color, alpha=0.65, label=f"+ c_coord thr {thr:g}")
    ax.set_xlabel("PKA energy (keV)")
    ax.set_ylabel("Defect-weighted reduction vs raw WS")
    ax.set_ylim(0, 1)
    ax.legend()
    save(fig, "fig_reduction_vs_energy")


# ---------------------------------------------------------------------------
# Figure 4 (MAIN): temporal evolution for a representative energy.
# Headline curves are raw WS and the Frenkel-corrected count (no mask); the
# structural mask is shown only as a lighter sensitivity band (c_coord thr
# 0.3..0.7) rather than as a definitive answer, because in dense cascade cores
# it removes a large fraction of what may be genuine surviving damage.
# ---------------------------------------------------------------------------
def fig_main(timeseries, energy):
    raw = first_per_group(filter_rows(timeseries, energy=energy),
                          ["timestep"], "raw_ws_vacancies")
    raw = sorted(raw, key=lambda r: r["timestep"])
    if not raw:
        print(f"  skip fig_main: no rows for {energy}")
        return
    t_raw = [r["timestep"] for r in raw]

    fig, ax = plt.subplots()
    ax.plot(t_raw, [r["raw_ws_vacancies"] for r in raw], marker="o",
            color="#b02418", label="raw WS")

    # c_coord structural-mask sensitivity band (thr 0.3 lower .. 0.7 upper).
    def vac_series(thr):
        sub = sorted(filter_rows(timeseries, energy=energy, mask_signal=CCOORD,
                                 mask_threshold=thr), key=lambda r: r["timestep"])
        return {r["timestep"]: r["vacancy_count"] for r in sub}
    lo, hi = vac_series(0.3), vac_series(0.7)
    common_t = [t for t in t_raw if t in lo and t in hi]
    if common_t:
        ax.fill_between(common_t, [lo[t] for t in common_t], [hi[t] for t in common_t],
                        color="#1f4eb0", alpha=0.18,
                        label="c_coord mask (thr 0.3–0.7)")

    # Headline corrected curve: Frenkel only, no structural mask.
    nomask_path = os.path.join(SWEEP_DIR, f"{energy}_nomask.csv")
    if os.path.exists(nomask_path):
        nm = [r for r in read_csv_rows(nomask_path) if r["status_type"] == "success"]
        nm = sorted(nm, key=lambda r: r["timestep"])
        ax.plot([r["timestep"] for r in nm], [r["vacancy_count"] for r in nm], marker="s",
                color="#222222", label="Frenkel-corrected (no mask)")

    ax.set_xlabel("Timestep")
    ax.set_ylabel("Vacancy count")
    ax.set_ylim(bottom=0)
    ax.legend()
    save(fig, f"fig_main_{energy}")


# ---------------------------------------------------------------------------
# Figure 5: stacked stage budget over time for a representative energy.
# ---------------------------------------------------------------------------
def fig_stage_fractions(timeseries, energy, thr=0.5):
    sub = filter_rows(timeseries, energy=energy, mask_signal=CCOORD, mask_threshold=thr)
    sub = sorted([r for r in sub if r["raw_ws_vacancies"] > 0], key=lambda r: r["timestep"])
    if not sub:
        print(f"  skip fig_stage_fractions: no nonzero-raw rows for {energy}")
        return

    raw = np.array([r["raw_ws_vacancies"] for r in sub])
    stages = {
        "Frenkel recombined": np.array([r["frenkel_recombined"] for r in sub]) / raw,
        "No free volume": np.array([r["freevolume_excluded"] for r in sub]) / raw,
        "Surface/void": np.array([r["surface_or_void_excluded"] for r in sub]) / raw,
        "Structural": np.array([r["structural_excluded"] for r in sub]) / raw,
        "Surviving vacancy": np.array([r["vacancy_count"] for r in sub]) / raw,
    }
    colors = ["#d08a10", "#8a5a2a", "#999999", "#2a8a3e", "#b02418"]

    fig, ax = plt.subplots()
    ax.stackplot([r["timestep"] for r in sub], stages.values(), labels=stages.keys(),
                 colors=colors, alpha=0.9)
    ax.set_xlabel("Timestep")
    ax.set_ylabel("Fraction of raw WS vacancies")
    ax.set_ylim(0, 1)
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.22), ncol=2)
    save(fig, f"fig_stage_fractions_{energy}")


# ---------------------------------------------------------------------------
# Figure 6: Frenkel-cutoff sensitivity (optional; needs the sensitivity sweep).
# ---------------------------------------------------------------------------
def fig_cutoff_sensitivity(energies):
    sens_dir = os.path.join(SWEEP_DIR, "sensitivity")
    fig, ax = plt.subplots()
    plotted = False
    for energy in energies:
        path = os.path.join(sens_dir, f"{energy}_cutoff.csv")
        if not os.path.exists(path):
            continue
        df = [r for r in read_csv_rows(path) if r["status_type"] == "success"]
        # peak vacancy_count across frames for each cutoff value
        by_cutoff = defaultdict(list)
        for r in df:
            if r["recombination_cutoff"] == "auto":
                continue
            by_cutoff[float(r["recombination_cutoff"])].append(r["vacancy_count"])
        g = sorted((cutoff, max(values)) for cutoff, values in by_cutoff.items())
        ax.plot([x for x, _ in g], [y for _, y in g], marker="o", label=energy)
        plotted = True
    if not plotted:
        print("  skip fig_cutoff_sensitivity: no sensitivity/<energy>_cutoff.csv found")
        plt.close(fig)
        return
    ax.set_xlabel(r"Frenkel cutoff ($\AA$)")
    ax.set_ylabel("Peak vacancy count")
    ax.set_ylim(bottom=0)
    ax.legend()
    save(fig, "fig_cutoff_sensitivity")


# ---------------------------------------------------------------------------
# Synthetic ground-truth figures: precision / recall / F1 vs thermal noise.
# This is the credibility anchor -- it licenses the reduction claim by showing,
# against known vacancies, that the method preserves true vacancies while
# rejecting the spurious WS pairs that thermal noise creates.
# ---------------------------------------------------------------------------
def fig_metric_vs_noise(metric, ylabel):
    path = os.path.join(SYNTH_DIR, "precision_recall.csv")
    if not os.path.exists(path):
        print(f"  skip fig_{metric}_vs_noise: run validate_precision_recall.py first")
        return
    rows = read_csv_rows(path)
    fig, ax = plt.subplots()
    for predictor, (color, marker) in PREDICTOR_STYLE.items():
        sub = sorted([r for r in rows if r["predictor"] == predictor],
                     key=lambda r: r["sigma"])
        if not sub:
            continue
        ax.plot([r["sigma"] for r in sub], [r[metric] for r in sub],
                marker=marker, color=color, label=predictor)
    ax.set_xlabel(r"Thermal noise $\sigma$ (units of $r_{nn}$)")
    ax.set_ylabel(ylabel)
    ax.set_ylim(0, 1.02)
    ax.legend()
    save(fig, f"fig_{metric}_vs_noise")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--energy", default="7kev",
                    help="Representative energy for temporal/stage figures.")
    ap.add_argument("--sensitivity-energies", nargs="*",
                    default=["1kev", "5kev", "7kev", "8kev_real"])
    args = ap.parse_args()

    summary = load_summary()
    timeseries = load_timeseries()
    nomask_summary = load_nomask_summary()

    print(f"# figures -> {FIG_DIR}")
    # Credibility anchor (synthetic ground truth) first.
    fig_metric_vs_noise("precision", "Vacancy precision")
    fig_metric_vs_noise("recall", "Vacancy recall")
    fig_metric_vs_noise("f1", "Vacancy F1 score")
    # Real-data impact + mechanism.
    fig_main(timeseries, args.energy)
    fig_reduction_vs_energy(summary, nomask_summary)
    fig_energy_vs_raw_ws(summary)
    fig_energy_vs_vacancy_ccoord(summary)
    fig_stage_fractions(timeseries, args.energy)
    fig_cutoff_sensitivity(args.sensitivity_energies)
    print("# done.")


if __name__ == "__main__":
    main()
