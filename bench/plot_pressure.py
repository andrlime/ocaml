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

"""Figure F5: the win grows as DRAM gets scarcer. For one benchmark, slowdown
vs the unbounded all_dram baseline as the DRAM cap tightens, one line per
policy. The naive all_far stays flat-bad; criticality-aware policies degrade
gently, so the gap widens with pressure.

    python plot_pressure.py placement.csv pressure.png [bench]
"""

import sys
import matplotlib.pyplot as plt

import plotting


def main(csv_path, out_path, bench=None):
    plotting.apply_style()
    df = plotting.load_placement(csv_path)
    if bench is None:
        bench = plotting.default_bench(df)
    df = df[df["bench"] == bench]
    med = plotting.median_by(df, ["policy", "dram_cap"])

    # Baseline: unbounded all_dram (cap 0 = no cap).
    base_row = med[(med.policy == "all_dram") & (med.dram_cap == 0)]["wall_ms"]
    base = float(base_row.iloc[0]) if len(base_row) else \
        float(med["wall_ms"].min())

    # x axis: caps from largest (least pressure) to smallest (most), with 0
    # plotted as the largest since it means "unbounded".
    caps = sorted(c for c in med["dram_cap"].unique() if c != 0)
    policies = plotting.ordered_policies(med["policy"].unique())

    fig, ax = plt.subplots(figsize=(11, 6))
    for pol in policies:
        xs, ys = [], []
        for c in caps:
            row = med[(med.policy == pol) & (med.dram_cap == c)]["wall_ms"]
            if len(row):
                xs.append(c / 1e6)
                ys.append(float(row.iloc[0]) / base)
        if xs:
            ax.plot(xs, ys, marker="o", label=pol,
                    color=plotting.policy_color(pol))

    ax.invert_xaxis()  # tighter cap (more pressure) to the right
    ax.axhline(1.0, color="#666666", linewidth=1, linestyle="--")
    ax.set_xlabel("DRAM cap (M words)  -- pressure increases right")
    ax.set_ylabel("slowdown vs unbounded all_dram")
    ax.set_title("Sensitivity to DRAM pressure (%s)" % bench)
    ax.legend(title=None)
    plotting.save(fig, out_path)


if __name__ == "__main__":
    csv = sys.argv[1] if len(sys.argv) > 1 else "results/placement.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "pressure.png"
    bench = sys.argv[3] if len(sys.argv) > 3 else None
    main(csv, out, bench)
