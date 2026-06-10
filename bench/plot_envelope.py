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

"""Figure A1: the placement envelope -- all_dram (upper bound) vs all_far
(naive control). The two bookend conditions, per benchmark, as raw wall time.
The gap between them is the budget that smart placement plays within.

    python plot_envelope.py placement.csv envelope.png
"""

import sys
import matplotlib.pyplot as plt

import plotting


def main(csv_path, out_path):
    plotting.apply_style()
    df = plotting.load_placement(csv_path)
    med = plotting.median_by(df, ["bench", "policy", "dram_cap"])
    benches = sorted(df["bench"].unique())
    pair = ["all_dram", "all_far"]

    def wall(b, pol):
        # Loosest cap, so the bookends reflect pure placement, not a cap.
        cap = plotting.nearest_cap(df, b, 1.0)
        row = med[(med.bench == b) & (med.policy == pol)
                  & (med.dram_cap == cap)]["wall_ms"]
        return float(row.iloc[0]) if len(row) else float("nan")

    fig, ax = plt.subplots(figsize=(11, 6.5))
    width = 0.36
    series = {}
    for i, pol in enumerate(pair):
        ys = [wall(b, pol) for b in benches]
        series[pol] = ys
        xs = [j + (i - 0.5) * width for j in range(len(benches))]
        ax.bar(xs, ys, width=width, label=pol, color=plotting.policy_color(pol))

    # Annotate how much slower all_far is than all_dram for each benchmark.
    for j, b in enumerate(benches):
        lo, hi = series["all_dram"][j], series["all_far"][j]
        if lo and lo == lo:
            ax.text(j, hi, "all_far %.2f×" % (hi / lo),
                    ha="center", va="bottom", fontsize=10)

    ax.set_xticks(range(len(benches)))
    ax.set_xticklabels(benches)
    ax.set_ylabel("wall time (ms)")
    ax.set_title("The placement envelope: all-DRAM vs all-far")
    ax.legend(title=None)
    plotting.save(fig, out_path)


if __name__ == "__main__":
    csv = sys.argv[1] if len(sys.argv) > 1 else "results/placement.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "envelope.png"
    main(csv, out)
