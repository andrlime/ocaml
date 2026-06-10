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

"""Figure F6: the steering is real. Committed words per arena (DRAM vs far) for
each policy on one benchmark -- proof that placement genuinely differs across
policies, not just timing noise.

    python plot_occupancy.py placement.csv occupancy.png [bench]
"""

import sys
import matplotlib.pyplot as plt

import plotting


def main(csv_path, out_path, bench=None):
    plotting.apply_style()
    df = plotting.load_placement(csv_path)
    # Unbounded DRAM if available: each policy then places by its own logic,
    # so the arena split reflects the policy and not a cap forcing spills.
    caps = set(df["dram_cap"].unique())
    cap = 0 if 0 in caps else plotting.most_pressure_cap(df)
    if bench is None:
        bench = plotting.default_bench(df)
    df = df[(df["dram_cap"] == cap) & (df["bench"] == bench)]

    med = plotting.median_by(df, ["policy"], "dram_committed").merge(
        plotting.median_by(df, ["policy"], "far_committed"), on="policy")
    policies = plotting.ordered_policies(med["policy"].unique())

    fig, ax = plt.subplots(figsize=(11, 6))
    scale = 1e6  # words -> millions of words
    dram = [float(med[med.policy == p]["dram_committed"].iloc[0]) / scale
            for p in policies]
    far = [float(med[med.policy == p]["far_committed"].iloc[0]) / scale
           for p in policies]
    ax.bar(policies, dram, label="DRAM", color=plotting.tier_color("dram"))
    ax.bar(policies, far, bottom=dram, label="far",
           color=plotting.tier_color("far"))

    ax.set_ylabel("committed (M words)")
    ax.set_title("Arena occupancy by policy (%s, DRAM cap: %s)"
                 % (bench, plotting.cap_label(cap)))
    ax.legend(title=None)
    plotting.save(fig, out_path)


if __name__ == "__main__":
    csv = sys.argv[1] if len(sys.argv) > 1 else "results/placement.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "occupancy.png"
    bench = sys.argv[3] if len(sys.argv) > 3 else None
    main(csv, out, bench)
