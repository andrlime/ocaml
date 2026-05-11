"""Plots write-bandwidth or heap-promotion benchmark results.

Usage:
    uv run main.py                      # runs ./write_bw, saves results.tsv
    uv run main.py results.tsv          # raw mmap benchmark (seq + rand patterns)
    uv run main.py heap_results.tsv     # OCaml heap promotion benchmark
"""

import subprocess
import sys
import os
import csv
import io

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SRC     = os.path.join(SCRIPT_DIR, "../testsuite/tests/tiered-heap/write_bw.c")
BIN     = os.path.join(SCRIPT_DIR, "write_bw")
TSV_OUT = os.path.join(SCRIPT_DIR, "results.tsv")

COLORS = {"DRAM": "#4C72B0", "FAR": "#DD8452"}

err_kw = dict(ecolor="black", capsize=7, elinewidth=2.0, capthick=2.0)


# ---------------------------------------------------------------------------
def build():
    r = subprocess.run(["gcc", "-O2", "-mclwb", "-o", BIN, SRC, "-lm", "-lnuma"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print("Build failed:\n" + r.stderr, file=sys.stderr)
        sys.exit(1)


def run_bench() -> str:
    r = subprocess.run([BIN], capture_output=True, text=True)
    if r.stderr:
        print(r.stderr.strip(), file=sys.stderr)
    raw = r.stdout
    with open(TSV_OUT, "w") as f:
        f.write(raw)
    return raw


def load_tsv(path: str) -> str:
    with open(path) as f:
        return f.read()


def parse(raw: str) -> dict:
    """Returns {arena: {pattern: {bw, bw_std, lat, lat_std}}}."""
    results: dict = {}
    reader = csv.DictReader(io.StringIO(raw), delimiter="\t")
    for row in reader:
        results.setdefault(row["arena"], {})[row["pattern"]] = {
            "bw":      float(row["bw_mean"]),
            "bw_std":  float(row["bw_std"]),
            "lat":     float(row["lat_mean"]),
            "lat_std": float(row["lat_std"]),
        }
    return results


# ---------------------------------------------------------------------------
def plot_multi_pattern(results: dict, patterns: list[str],
                       pattern_labels: list[str], png_out: str, subtitle: str):
    """Bar chart with one group per pattern, one bar per arena."""
    arenas  = [a for a in ["DRAM", "FAR"] if a in results]
    x       = np.arange(len(patterns))
    width   = 0.35
    offsets = np.linspace(-(len(arenas)-1)/2, (len(arenas)-1)/2, len(arenas)) * width

    fig, (ax_bw, ax_lat) = plt.subplots(1, 2, figsize=(11, 4.5))
    fig.suptitle("Write bandwidth and latency: DRAM vs FAR (devdax)",
                 fontsize=13, fontweight="bold")

    for i, arena in enumerate(arenas):
        color = COLORS.get(arena, "#888")
        for pi, pattern in enumerate(patterns):
            entry = results[arena].get(pattern,
                        {"bw": 0, "bw_std": 0, "lat": 0, "lat_std": 0})
            kw = dict(color=color, edgecolor="white", linewidth=0.5)
            ax_bw.bar(x[pi] + offsets[i],  entry["bw"],  width,
                      yerr=entry["bw_std"],  error_kw=err_kw, **kw)
            ax_lat.bar(x[pi] + offsets[i], entry["lat"], width,
                       yerr=entry["lat_std"], error_kw=err_kw, **kw)

    for ax, ylabel, title in [
        (ax_bw,  "GiB/s",          "Write bandwidth"),
        (ax_lat, "ns / cacheline", "Write latency  (lower is better)"),
    ]:
        ax.set_ylabel(ylabel, fontsize=11)
        ax.set_title(title, fontsize=11)
        ax.set_xticks(x)
        ax.set_xticklabels(pattern_labels)
        ax.grid(axis="y", linestyle="--", alpha=0.4)
        ax.spines[["top", "right"]].set_visible(False)

    _add_legend(fig, arenas, subtitle)
    plt.tight_layout(rect=[0, 0.04, 1, 1])
    plt.savefig(png_out, dpi=150, bbox_inches="tight")
    print(f"Saved: {png_out}")


def plot_single_pattern(results: dict, pattern: str,
                        png_out: str, subtitle: str):
    """Side-by-side bars: one bar per arena, bandwidth left / latency right."""
    arenas = [a for a in ["DRAM", "FAR"] if a in results]
    x      = np.arange(len(arenas))
    width  = 0.5

    fig, (ax_bw, ax_lat) = plt.subplots(1, 2, figsize=(9, 4.5))
    fig.suptitle("OCaml heap promotion: DRAM vs FAR (devdax)",
                 fontsize=13, fontweight="bold")

    bw_vals  = [results[a][pattern]["bw"]      for a in arenas]
    bw_errs  = [results[a][pattern]["bw_std"]  for a in arenas]
    lat_vals = [results[a][pattern]["lat"]     for a in arenas]
    lat_errs = [results[a][pattern]["lat_std"] for a in arenas]
    colors   = [COLORS.get(a, "#888") for a in arenas]

    ax_bw.bar(x, bw_vals,  width, color=colors, yerr=bw_errs,
              error_kw=err_kw, edgecolor="white", linewidth=0.5)
    ax_lat.bar(x, lat_vals, width, color=colors, yerr=lat_errs,
               error_kw=err_kw, edgecolor="white", linewidth=0.5)

    for ax, ylabel, title in [
        (ax_bw,  "GiB/s",        "Promotion write bandwidth"),
        (ax_lat, "ns / object",  "Promotion latency  (lower is better)"),
    ]:
        ax.set_ylabel(ylabel, fontsize=11)
        ax.set_title(title, fontsize=11)
        ax.set_xticks(x)
        ax.set_xticklabels(arenas, fontsize=12)
        ax.grid(axis="y", linestyle="--", alpha=0.4)
        ax.spines[["top", "right"]].set_visible(False)

    _add_legend(fig, arenas, subtitle)
    plt.tight_layout(rect=[0, 0.04, 1, 1])
    plt.savefig(png_out, dpi=150, bbox_inches="tight")
    print(f"Saved: {png_out}")


def _add_legend(fig, arenas, subtitle):
    handles = [mpatches.Patch(facecolor=COLORS.get(a, "#888"), label=a)
               for a in arenas]
    fig.legend(handles=handles, loc="lower center",
               bbox_to_anchor=(0.5, -0.06), ncol=len(arenas),
               framealpha=0.9, fontsize=10,
               title=subtitle, title_fontsize=8)


# ---------------------------------------------------------------------------
if __name__ == "__main__":
    if len(sys.argv) > 1:
        tsv_path = sys.argv[1]
        raw = load_tsv(tsv_path)
    else:
        build()
        raw = run_bench()
        tsv_path = TSV_OUT

    results = parse(raw)
    png_out = os.path.splitext(tsv_path)[0] + ".png"

    # Collect all pattern names across all arenas
    all_patterns = list(dict.fromkeys(
        p for arena in results.values() for p in arena
    ))

    print("\nResults:")
    for arena, patterns in results.items():
        for pattern, d in patterns.items():
            print(f"  {arena:6s}  {pattern:12s}  "
                  f"bw={d['bw']:.2f}±{d['bw_std']:.2f} GiB/s  "
                  f"lat={d['lat']:.1f}±{d['lat_std']:.1f} ns/obj")

    if len(all_patterns) == 1:
        subtitle = "2M objects × 64 B · 30 runs · error bars = ±SEM  |  path: oldify_one → caml_shared_try_alloc_arena"
        plot_single_pattern(results, all_patterns[0], png_out, subtitle)
    else:
        labels = {"seq-write": "Sequential", "rand-write": "Random",
                  "promote": "Promote"}
        pattern_labels = [labels.get(p, p) for p in all_patterns]
        subtitle = "256 MiB region · 5 runs · 64 B cacheline · error bars = ±1σ"
        plot_multi_pattern(results, all_patterns, pattern_labels, png_out, subtitle)
