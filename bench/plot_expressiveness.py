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

"""Figure F1: expressiveness. A table of the policies the framework hosts --
what each decides on, how many lines its decision function is, and which
authoring surface it uses (built-in C, dlopen .so, or compiler attribute). The
point: many distinct policies, each tiny, across three authoring modes, with no
engine changes.

The LoC counts are the bodies of the policies in runtime/placement.c and
examples/policies/; update them if those change.

    python plot_expressiveness.py expressiveness.png
"""

import sys
import matplotlib.pyplot as plt

import plotting

# policy, decides on, decision-fn LoC, authoring surface
ROWS = [
    ("all_dram",       "-- (baseline)",        1, "built-in"),
    ("all_far",        "-- (control)",         1, "built-in"),
    ("flat_far",       "scannable bit",        1, "built-in"),
    ("size_threshold", "wosize + config",      3, "built-in"),
    ("attribute",      "compiler hint",        4, "built-in"),
    ("string_far",     "tag",                  8, "dlopen .so"),
    ("dram_budget",    "arena_used + config", 12, "dlopen .so"),
    ("[@far_memory]",  "source annotation",   "-", "attribute"),
]
COLUMNS = ["policy", "decides on", "decision LoC", "authoring"]


def main(out_path):
    plotting.apply_style()
    fig, ax = plt.subplots(figsize=(10, 5))
    ax.axis("off")

    cells = [[r[0], r[1], str(r[2]), r[3]] for r in ROWS]
    table = ax.table(cellText=cells, colLabels=COLUMNS, loc="center",
                     cellLoc="left", colLoc="left")
    table.auto_set_font_size(False)
    table.set_fontsize(12)
    table.scale(1.0, 1.7)

    # Colour each row's left cell by authoring surface for a quick read.
    surface_color = {"built-in": "#029E73", "dlopen .so": "#0173B2",
                     "attribute": "#CA9161"}
    for i, r in enumerate(ROWS, start=1):
        table[(i, 0)].set_facecolor(surface_color[r[3]])
        table[(i, 0)].set_alpha(0.25)

    ax.set_title("Policies the framework hosts", pad=20)
    plotting.save(fig, out_path)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "expressiveness.png"
    main(out)
