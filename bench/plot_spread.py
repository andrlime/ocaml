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


TARGET_FRACTION = 0.5   # cap each benchmark to ~half its footprint


def main(csv_path, out_path):
    plotting.apply_style()
    df = plotting.load_placement(csv_path)
    benches = sorted(df["bench"].unique())
    med = plotting.median_by(df, ["bench", "policy", "dram_cap"])

    # Baseline per bench: all_dram at the loosest cap (fits in DRAM).
    def baseline(b):
        loose = plotting.nearest_cap(df, b, 1.0)
        row = med[(med.bench == b) & (med.policy == "all_dram")
                  & (med.dram_cap == loose)]["wall_ms"]
        return float(row.iloc[0])

    bases = {b: baseline(b) for b in benches}
    policies = plotting.ordered_policies(med["policy"].unique())

    fig, ax = plt.subplots(figsize=(12, 6.5))
    n = len(policies)
    width = 0.8 / n
    for i, pol in enumerate(policies):
        ys = []
        for b in benches:
            cap = plotting.nearest_cap(df, b, TARGET_FRACTION)
            cur = med[(med.bench == b) & (med.policy == pol)
                      & (med.dram_cap == cap)]["wall_ms"]
            ys.append(float(cur.iloc[0]) / bases[b]
                      if len(cur) else float("nan"))
        xs = [j + (i - (n - 1) / 2) * width for j in range(len(benches))]
        ax.bar(xs, ys, width=width, label=pol, color=plotting.policy_color(pol))

    ax.axhline(1.0, color="#666666", linewidth=1, linestyle="--")
    ax.text(len(benches) - 0.5, 1.0, "all_dram, no pressure ",
            va="bottom", ha="right", color="#666666", fontsize=10)
    ax.set_xticks(range(len(benches)))
    ax.set_xticklabels(benches)
    ax.set_ylabel("wall time vs all-in-DRAM\n(1.0 = same, 2.0 = 2× slower)")
    ax.set_title("Policy spread at a ~50%-of-footprint DRAM cap")
    ax.legend(title=None, ncol=len(policies))
    plotting.save(fig, out_path)


if __name__ == "__main__":
    csv = sys.argv[1] if len(sys.argv) > 1 else "results/placement.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "spread.png"
    main(csv, out)
