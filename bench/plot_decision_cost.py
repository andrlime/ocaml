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

"""Figure F3: one placement decision costs a couple of nanoseconds.

Reads decisionbench output (path,ns_per_call) and draws the cost of calling the
policy three ways: inlinable (the floor), through a function pointer (the
built-in dispatch), and through a dlopen'd .so (the loadable path). The bars
being a few ns -- and dlopen barely above the built-in -- is the point:
programmability is effectively free per object.

    python plot_decision_cost.py decision.csv decision_cost.png
"""

import sys
import pandas as pd
import matplotlib.pyplot as plt

import plotting

LABELS = {
    "inlinable": "inlinable\n(floor)",
    "indirect": "function pointer\n(built-in)",
    "dlopen": "dlopen .so\n(loadable)",
}
COLORS = {"inlinable": "#949494", "indirect": "#029E73", "dlopen": "#0173B2"}
ORDER = ["inlinable", "indirect", "dlopen"]


def main(csv_path, out_path):
    plotting.apply_style()
    df = pd.read_csv(csv_path)
    paths = [p for p in ORDER if p in set(df["path"])]
    vals = [float(df[df.path == p]["ns_per_call"].iloc[0]) for p in paths]

    fig, ax = plt.subplots(figsize=(9, 6))
    bars = ax.bar([LABELS[p] for p in paths], vals,
                  width=0.6, color=[COLORS[p] for p in paths])
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v, "%.1f ns" % v,
                ha="center", va="bottom")

    ax.set_ylabel("time per decision (ns)")
    ax.set_ylim(0, max(vals) * 1.25)
    ax.set_title("Cost of one placement decision")
    plotting.save(fig, out_path)


if __name__ == "__main__":
    csv = sys.argv[1] if len(sys.argv) > 1 else "results/decision.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "decision_cost.png"
    main(csv, out)
