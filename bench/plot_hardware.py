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

"""Figure H1: the DRAM and far-memory tiers genuinely differ.

Reads the CSV from membench (tier,metric,value,unit) and draws two panels --
random-access latency (lower is better) and sequential bandwidth (higher is
better) -- side by side, DRAM vs far.

    python plot_hardware.py membench.csv hardware.png
"""

import sys
import pandas as pd
import matplotlib.pyplot as plt

import plotting


def main(csv_path, out_path):
    plotting.apply_style()
    df = pd.read_csv(csv_path)
    tiers = ["dram", "far"]
    present = [t for t in tiers if t in set(df["tier"])]
    colors = [plotting.tier_color(t) for t in present]

    fig, (ax_lat, ax_bw) = plt.subplots(1, 2, figsize=(13, 5.5))

    # Latency panel.
    lat = df[df["metric"] == "latency"].set_index("tier")["value"]
    ax_lat.bar(present, [lat[t] for t in present], color=colors, width=0.6)
    ax_lat.set_ylabel("latency (ns/access)")
    ax_lat.set_title("Random-access latency")
    for i, t in enumerate(present):
        ax_lat.text(i, lat[t], "%.0f" % lat[t], ha="center", va="bottom")

    # Bandwidth panel: read and write side by side per tier.
    x = range(len(present))
    w = 0.38
    rbw = df[df["metric"] == "read_bw"].set_index("tier")["value"]
    wbw = df[df["metric"] == "write_bw"].set_index("tier")["value"]
    ax_bw.bar([i - w / 2 for i in x], [rbw[t] for t in present], width=w,
              label="read", color=colors)
    ax_bw.bar([i + w / 2 for i in x], [wbw[t] for t in present], width=w,
              label="write", color=colors, alpha=0.55)
    ax_bw.set_xticks(list(x))
    ax_bw.set_xticklabels(present)
    ax_bw.set_ylabel("bandwidth (GB/s)")
    ax_bw.set_title("Sequential bandwidth")
    ax_bw.legend(title=None)

    plotting.save(fig, out_path)


if __name__ == "__main__":
    csv = sys.argv[1] if len(sys.argv) > 1 else "membench.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "hardware.png"
    main(csv, out)
