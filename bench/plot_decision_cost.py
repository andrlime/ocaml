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

Reads decisionbench output (path,ns_per_call). Both bars call a policy through
its ops table -- the exact indirection the engine performs -- differing only in
where the policy lives: compiled into the binary (built-in) vs loaded from a
.so (dlopen). They are equal at a couple of ns, so making placement
programmable via a loadable policy costs nothing per object.

    python plot_decision_cost.py decision.csv decision_cost.png
"""

import sys
import pandas as pd
import matplotlib.pyplot as plt

import plotting

LABELS = {
    "builtin": "built-in\n(compiled in)",
    "dlopen": "dlopen .so\n(loadable)",
}
COLORS = {"builtin": "#029E73", "dlopen": "#0173B2"}
ORDER = ["builtin", "dlopen"]


def main(csv_path, out_path):
    plotting.apply_style()
    df = pd.read_csv(csv_path)
    paths = [p for p in ORDER if p in set(df["path"])]

    meds, los, his = [], [], []
    for p in paths:
        v = df[df.path == p]["ns_per_call"]
        m = float(v.median())
        meds.append(m)
        los.append(m - float(v.quantile(0.25)))
        his.append(float(v.quantile(0.75)) - m)

    n = int(df.groupby("path").size().max())
    fig, ax = plt.subplots(figsize=(9, 6))
    bars = ax.bar([LABELS[p] for p in paths], meds, width=0.6,
                  color=[COLORS[p] for p in paths],
                  yerr=[los, his], capsize=6)
    for b, m, hi in zip(bars, meds, his):
        ax.text(b.get_x() + b.get_width() / 2, m + hi, "%.2f ns" % m,
                ha="center", va="bottom")

    ax.set_ylabel("time per decision (ns)")
    ax.set_ylim(0, max(m + h for m, h in zip(meds, his)) * 1.25)
    ax.set_title("Cost of one placement decision (median of %d runs, IQR)" % n)
    plotting.save(fig, out_path)


if __name__ == "__main__":
    csv = sys.argv[1] if len(sys.argv) > 1 else "results/decision.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "decision_cost.png"
    main(csv, out)
