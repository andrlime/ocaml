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
    cap = plotting.most_pressure_cap(df)
    df = df[df["dram_cap"] == cap]
    med = plotting.median_by(df, ["bench", "policy"])

    benches = sorted(med["bench"].unique())
    pair = ["all_dram", "all_far"]

    fig, ax = plt.subplots(figsize=(11, 6))
    width = 0.36
    for i, pol in enumerate(pair):
        ys = [float(med[(med.bench == b) & (med.policy == pol)]["wall_ms"]
                    .iloc[0]) for b in benches]
        xs = [j + (i - 0.5) * width for j in range(len(benches))]
        ax.bar(xs, ys, width=width, label=pol, color=plotting.policy_color(pol))

    ax.set_xticks(range(len(benches)))
    ax.set_xticklabels(benches)
    ax.set_ylabel("wall time (ms)")
    ax.set_title("Upper bound vs naive control (DRAM cap: %s)"
                 % plotting.cap_label(cap))
    ax.legend(title=None)
    plotting.save(fig, out_path)


if __name__ == "__main__":
    csv = sys.argv[1] if len(sys.argv) > 1 else "results/placement.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "envelope.png"
    main(csv, out)
