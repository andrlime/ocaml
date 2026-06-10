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

"""Figure F2: the framework is free when you are not steering.

Each benchmark compiled and run two ways: against stock OCaml (no placement
engine) and against this branch under all_dram (the engine active, but every
object still goes to DRAM, so behaviour matches stock). Bars are wall time
normalised to stock = 1.0; all_dram sitting on the line means the framework
adds no measurable cost. Error bars are the inter-quartile range over reps.

    python plot_overhead.py overhead.csv overhead.png

Expects policy values "stock" and "all_dram".
"""

import sys
import matplotlib.pyplot as plt

import plotting


def main(csv_path, out_path):
    plotting.apply_style()
    df = plotting.load_placement(csv_path)
    benches = sorted(df["bench"].unique())
    conds = ["stock", "all_dram"]

    fig, ax = plt.subplots(figsize=(11, 6))
    width = 0.36
    for i, cond in enumerate(conds):
        norms, los, his = [], [], []
        for b in benches:
            stock = df[(df.bench == b) & (df.policy == "stock")]["wall_ms"]
            cur = df[(df.bench == b) & (df.policy == cond)]["wall_ms"]
            base = float(stock.median())
            norms.append(float(cur.median()) / base)
            los.append((float(cur.median()) - float(cur.quantile(0.25))) / base)
            his.append((float(cur.quantile(0.75)) - float(cur.median())) / base)
        xs = [j + (i - 0.5) * width for j in range(len(benches))]
        ax.bar(xs, norms, width=width, label=cond,
               color=plotting.policy_color(cond),
               yerr=[los, his], capsize=4)

    ax.axhline(1.0, color="#666666", linewidth=1, linestyle="--")
    ax.set_xticks(range(len(benches)))
    ax.set_xticklabels(benches)
    ax.set_ylabel("wall time (normalised to stock)")
    ax.set_title("Framework overhead vs stock OCaml")
    ax.legend(title=None)
    plotting.save(fig, out_path)


if __name__ == "__main__":
    csv = sys.argv[1] if len(sys.argv) > 1 else "results/overhead.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "overhead.png"
    main(csv, out)
