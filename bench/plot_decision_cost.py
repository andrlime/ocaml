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

"""Figure F3: a placement decision costs a few nanoseconds.

Reads a CSV with two conditions that make the *same* decisions and the same
(all-DRAM) placement: the static built-in policy and the identical logic loaded
via dlopen. Each makes the same number of decisions on a given benchmark, so
(wall_dlopen - wall_static) / decisions is the marginal cost of one placement
decision through the indirect (dlopen) path -- a direct, baseline-free measure
of how cheap a decision is. One bar per benchmark.

    python plot_decision_cost.py decision.csv decision_cost.png

Expects policy values "static" and "dlopen" and a populated decision_count
(runs collected with CAML_PLACEMENT_COUNT set).
"""

import sys
import matplotlib.pyplot as plt

import plotting


def main(csv_path, out_path):
    plotting.apply_style()
    df = plotting.load_placement(csv_path)
    wall = plotting.median_by(df, ["bench", "policy"], "wall_ms")
    dec = plotting.median_by(df, ["bench", "policy"], "decision_count")
    med = wall.merge(dec, on=["bench", "policy"])
    benches = sorted(med["bench"].unique())

    ns_per = []
    for b in benches:
        st = med[(med.bench == b) & (med.policy == "static")]
        dl = med[(med.bench == b) & (med.policy == "dlopen")]
        if not len(st) or not len(dl) or dl["decision_count"].iloc[0] == 0:
            ns_per.append(float("nan"))
            continue
        delta_ms = float(dl["wall_ms"].iloc[0]) - float(st["wall_ms"].iloc[0])
        ns_per.append(delta_ms * 1e6 / float(dl["decision_count"].iloc[0]))

    fig, ax = plt.subplots(figsize=(10, 6))
    ax.bar(benches, ns_per, width=0.55, color="#0173B2")
    for i, v in enumerate(ns_per):
        if v == v:  # not NaN
            ax.text(i, v, "%.1f" % v, ha="center", va="bottom")
    ax.set_ylabel("ns per decision (dlopen vs static)")
    ax.set_title("Marginal cost of one placement decision")
    plotting.save(fig, out_path)


if __name__ == "__main__":
    csv = sys.argv[1] if len(sys.argv) > 1 else "results/decision.csv"
    out = sys.argv[2] if len(sys.argv) > 2 else "decision_cost.png"
    main(csv, out)
