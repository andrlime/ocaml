#**************************************************************************
#*                                                                        *
#*                                 OCaml                                  *
#*                                                                        *
#*                               Andrew Li                                *
#*                                                                        *
#*   Copyright 2026 Andrew Li                                             *
#*                                                                        *
#*   All rights reserved.  This file is distributed under the terms of    *
#*   the GNU Lesser General Public License version 2.1, with the          *
#*   special exception on linking described in the file LICENSE.          *
#*                                                                        *
#**************************************************************************

"""Shared plotting style for the placement evaluation figures.

Deliberately plain matplotlib: stock fonts, no bold, just a clean grid and a
consistent colourblind-friendly palette so every figure reads the same way.
Colours are semantic -- all_dram is the green "good" upper bound, all_far the
red naive control, the steering policies sit between.
"""

import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns

# Order policies appear in, left to right, when all are shown.
POLICY_ORDER = [
    "all_dram", "size_threshold", "flat_far", "attribute", "all_far"]


def policy_label(name):
    """Strip any ':config' suffix so size_threshold:256 reads as the policy."""
    return name.split(":")[0]


def load_placement(csv_path):
    """Load a placement sweep CSV and normalise the policy column (dropping
    ':config' suffixes) so rows group by policy."""
    df = pd.read_csv(csv_path)
    df["policy"] = df["policy"].map(policy_label)
    return df


def median_by(df, keys, value="wall_ms"):
    """Median of [value] over reps, grouped by [keys]."""
    return df.groupby(keys, as_index=False)[value].median()


def ordered_policies(present):
    """[present] policies in the canonical order, unknowns appended."""
    known = [p for p in POLICY_ORDER if p in present]
    return known + [p for p in present if p not in known]


def most_pressure_cap(df):
    """The DRAM cap with the most memory pressure: the smallest non-zero cap
    (cap 0 means unbounded). Returns 0 if only the unbounded cap exists."""
    caps = [c for c in df["dram_cap"].unique() if c != 0]
    return min(caps) if caps else 0


def cap_label(cap):
    return "unbounded" if cap == 0 else "%d words" % cap


def default_bench(df):
    """Prefer the mixed workload (the headline) if present, else the first."""
    benches = set(df["bench"].unique())
    return "mixed" if "mixed" in benches else sorted(benches)[0]


def footprint(df, bench):
    """A benchmark's total live footprint in words: the most memory it ever
    commits across the two arenas (reached at the loosest cap)."""
    rows = df[df["bench"] == bench]
    return float((rows["dram_committed"] + rows["far_committed"]).max())


def cap_fraction(df, bench, cap):
    """A cap as a fraction of [bench]'s footprint (1.0 = fits entirely in DRAM,
    smaller = more pressure). cap 0 (unbounded) maps to 1.0."""
    fp = footprint(df, bench)
    if fp == 0:
        return 1.0
    return 1.0 if cap == 0 else cap / fp


def nearest_cap(df, bench, target_fraction):
    """The cap for [bench] whose fraction is closest to [target_fraction]."""
    caps = df[df["bench"] == bench]["dram_cap"].unique()
    return min(caps, key=lambda c: abs(cap_fraction(df, bench, c)
                                       - target_fraction))

# seaborn "colorblind" palette, assigned semantically and kept stable so a
# policy/tier is the same colour in every figure.
POLICY_COLORS = {
    "stock":          "#949494",  # grey: stock OCaml baseline
    "all_dram":       "#029E73",  # green: upper bound (all local)
    "all_far":        "#D55E00",  # red: naive control (all far)
    "flat_far":       "#0173B2",  # blue
    "size_threshold": "#CC78BC",  # purple
    "attribute":      "#CA9161",  # brown
}

TIER_COLORS = {
    "dram": "#0173B2",  # blue
    "far":  "#DE8F05",  # orange
}


def apply_style():
    """Apply the shared look: clean grid, colourblind palette, readable plain
    fonts (no bold), and generous spacing via constrained_layout so nothing
    crowds."""
    sns.set_theme(context="notebook", style="whitegrid", palette="colorblind")
    plt.rcParams.update({
        "axes.titleweight": "normal",
        "axes.labelweight": "normal",
        "font.weight": "normal",
        "axes.titlesize": 13,
        "axes.labelsize": 12,
        "xtick.labelsize": 11,
        "ytick.labelsize": 11,
        "legend.fontsize": 11,
        "axes.titlepad": 12,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "figure.constrained_layout.use": True,
        "figure.constrained_layout.h_pad": 0.12,
        "figure.constrained_layout.w_pad": 0.12,
        "figure.constrained_layout.hspace": 0.12,
        "figure.constrained_layout.wspace": 0.12,
        "figure.dpi": 120,
        "savefig.dpi": 200,
        "savefig.bbox": "tight",
        "savefig.pad_inches": 0.3,
    })


def policy_color(name):
    return POLICY_COLORS.get(name, "#444444")


def tier_color(name):
    return TIER_COLORS.get(name, "#444444")


def save(fig, path):
    fig.savefig(path)
    print("wrote " + path)
