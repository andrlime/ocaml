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

"""Figure F5: the advantage of criticality-aware placement grows as DRAM gets
scarcer. For one benchmark, wall time relative to fitting entirely in DRAM, as
the DRAM cap tightens from 100% of the footprint down. The naive all_far is
flat and slow; all_dram degrades because it spills arbitrarily once DRAM is
full; flat_far/attribute spill the right objects and degrade gently -- the gap
widens with pressure.

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

    # Baseline: the loosest cap (largest footprint fraction) -- fits in DRAM.
    fracs = {c: plotting.cap_fraction(df, bench, c)
             for c in med["dram_cap"].unique()}
    loosest = max(fracs, key=lambda c: fracs[c])
    base_row = med[(med.policy == "all_dram") & (med.dram_cap == loosest)]
    base = float(base_row["wall_ms"].iloc[0])

    policies = plotting.ordered_policies(med["policy"].unique())
    fig, ax = plt.subplots(figsize=(11, 6.5))
    for pol in policies:
        pts = []
        for c in med[med.policy == pol]["dram_cap"].unique():
            row = med[(med.policy == pol) & (med.dram_cap == c)]["wall_ms"]
            pts.append((fracs[c] * 100, float(row.iloc[0]) / base))
        pts.sort()
        if pts:
            xs, ys = zip(*pts)
            ax.plot(xs, ys, marker="o", label=pol,
                    color=plotting.policy_color(pol))

    ax.invert_xaxis()  # more pressure to the right
    ax.axhline(1.0, color="#666666", linewidth=1, linestyle="--")
    ax.set_xlabel("DRAM cap (% of footprint)   --   pressure increases →")
    ax.set_ylabel("wall time vs all-in-DRAM\n(1.0 = same, 2.0 = 2× slower)")
    ax.set_title("Sensitivity to DRAM pressure (%s)" % bench)
    ax.legend(title=None)
    plotting.save(fig, out_path)


if __name__ == "__main__":
    csv = sys.argv[1] if len(sys.argv) > 1 else "results/placement.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "pressure.png"
    bench = sys.argv[3] if len(sys.argv) > 3 else None
    main(csv, out, bench)
