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
import seaborn as sns

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
