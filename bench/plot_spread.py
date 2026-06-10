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

"""Figure F4: criticality-aware policies fill the placement envelope.

Per benchmark, slowdown of each policy relative to all_dram (the upper bound),
at the most-constrained DRAM cap in the data. Shows the spread across policies
and that flat_far/attribute beat the naive all_far control.

    python plot_spread.py placement.csv spread.png
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
    policies = plotting.ordered_policies(med["policy"].unique())

    fig, ax = plt.subplots(figsize=(12, 6))
    n = len(policies)
    width = 0.8 / n
    for i, pol in enumerate(policies):
        ys = []
        for b in benches:
            base = med[(med.bench == b) & (med.policy == "all_dram")]["wall_ms"]
            cur = med[(med.bench == b) & (med.policy == pol)]["wall_ms"]
            ys.append(float(cur.iloc[0]) / float(base.iloc[0])
                      if len(cur) and len(base) else float("nan"))
        xs = [j + (i - (n - 1) / 2) * width for j in range(len(benches))]
        ax.bar(xs, ys, width=width, label=pol, color=plotting.policy_color(pol))

    ax.axhline(1.0, color="#666666", linewidth=1, linestyle="--")
    ax.set_xticks(range(len(benches)))
    ax.set_xticklabels(benches)
    ax.set_ylabel("slowdown vs all_dram")
    ax.set_title("Placement policy spread (DRAM cap: %s)"
                 % plotting.cap_label(cap))
    ax.legend(title=None, ncol=len(policies))
    plotting.save(fig, out_path)


if __name__ == "__main__":
    csv = sys.argv[1] if len(sys.argv) > 1 else "results/placement.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "spread.png"
    main(csv, out)
