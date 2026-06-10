#!/bin/sh
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

# One-shot: build everything, collect the data, and render every figure into
# ~/figures. Run on the box with the far-memory device.
#
#   sh make_figures.sh
#
# Pin for stable numbers if your machine has the cores/NUMA for it:
#   numactl --cpunodebind=0 --membind=0 sh make_figures.sh
#
# Knobs (environment, all optional):
#   FAR_DEVICE   devdax device                         (default /dev/dax1.0)
#   SIZE         working-set size per benchmark        (default 4000000)
#   REPS         repetitions per cell                  (default 10)
#   CAPS         DRAM caps in words for the sweep (space-separated; 0=unbounded)
#   FIGDIR       output directory                      (default ~/figures)

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
FAR_DEVICE=${FAR_DEVICE:-/dev/dax1.0}
SIZE=${SIZE:-4000000}
REPS=${REPS:-10}
CAPS=${CAPS:-"0 64000000 32000000 16000000 8000000"}
FIGDIR=${FIGDIR:-$HOME/figures}
RESULTS="$HERE/results"

export CAML_FAR_DEVICE="$FAR_DEVICE"
mkdir -p "$RESULTS" "$FIGDIR"

run_py() { (cd "$HERE" && uv run python "$@"); }

echo "==> building benchmarks"
make -C "$HERE/benchmarks" >/dev/null
gcc -O2 -o "$HERE/membench" "$HERE/membench.c"
cc -shared -fPIC -I "$ROOT/runtime" -I "$ROOT/runtime/caml" \
   "$HERE/noop_policy.c" -o "$HERE/noop.so"

echo "==> H1: tier characterisation (membench)"
"$HERE/membench" "$FAR_DEVICE" 512 > "$RESULTS/membench.csv"

echo "==> placement sweep -> placement.csv"
export CAPS REPS SIZE
sh "$HERE/run.sh" > "$RESULTS/placement.csv"

echo "==> decision-cost runs (static vs dlopen, with counting) -> decision.csv"
{
  "$HERE/benchmarks/ptrchase" --header
  for bench in ptrchase arraytraverse mixed; do
    r=0
    while [ "$r" -lt "$REPS" ]; do
      BENCH_SIZE="$SIZE" BENCH_REP="$r" CAML_PLACEMENT_COUNT=1 \
        CAML_GC_POLICY=all_dram "$HERE/benchmarks/$bench" \
        | sed 's/,all_dram,/,static,/'
      BENCH_SIZE="$SIZE" BENCH_REP="$r" CAML_PLACEMENT_COUNT=1 \
        CAML_GC_POLICY="$HERE/noop.so" "$HERE/benchmarks/$bench" \
        | sed "s#,$HERE/noop.so,#,dlopen,#"
      r=$((r + 1))
    done
  done
} > "$RESULTS/decision.csv"

echo "==> rendering figures into $FIGDIR"
P="$RESULTS/placement.csv"
D="$RESULTS/decision.csv"
run_py plot_hardware.py "$RESULTS/membench.csv" "$FIGDIR/fig_h1_hardware.png"
run_py plot_expressiveness.py "$FIGDIR/fig_f1_expressiveness.png"
run_py plot_decision_cost.py "$D" "$FIGDIR/fig_f3_decision_cost.png"
run_py plot_envelope.py "$P" "$FIGDIR/fig_a1_envelope.png"
run_py plot_spread.py "$P" "$FIGDIR/fig_f4_spread.png"
run_py plot_pressure.py "$P" "$FIGDIR/fig_f5_pressure.png"
run_py plot_occupancy.py "$P" "$FIGDIR/fig_f6_occupancy.png"

echo "==> done. figures in $FIGDIR, raw data in $RESULTS"
