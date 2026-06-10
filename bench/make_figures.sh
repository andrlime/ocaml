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
#   SIZE         working-set size per benchmark        (default 2000000)
#   REPS         repetitions per cell                  (default 5)
#   FRACTIONS    cap points as % of each benchmark's footprint
#   POLICIES / BENCHES   override the sets swept
#   FIGDIR       output directory                      (default ~/figures)

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
FAR_DEVICE=${FAR_DEVICE:-/dev/dax1.0}
SIZE=${SIZE:-2000000}
REPS=${REPS:-5}
FIGDIR=${FIGDIR:-$HOME/figures}
RESULTS="$HERE/results"

# This is a real sweep -- 5 policies x 3 benchmarks x 7 cap points x reps -- and
# far-memory runs are slow (millions of dependent far-latency loads each), so
# expect tens of minutes on devdax. The sweep prints [cell/total] progress to
# stderr. Sanity-check first with a tiny, fast pass:
#   SIZE=300000 REPS=2 FRACTIONS="100 50" sh make_figures.sh

export CAML_FAR_DEVICE="$FAR_DEVICE"
mkdir -p "$RESULTS" "$FIGDIR"

run_py() { (cd "$HERE" && uv run python "$@"); }

POLICIES=${POLICIES:-"all_dram all_far flat_far size_threshold:32 attribute"}
BENCHES=${BENCHES:-"ptrchase arraytraverse mixed"}
# Cap points as a percentage of each benchmark's own footprint, so pressure is
# comparable across workloads with very different sizes. 100 ~ fits in DRAM.
FRACTIONS=${FRACTIONS:-"100 70 50 35 25 18 12"}
DIR="$HERE/benchmarks"

echo "==> building benchmarks"
make -C "$DIR" >/dev/null
gcc -O2 -o "$HERE/membench" "$HERE/membench.c"
cc -shared -fPIC -I "$ROOT/runtime" -I "$ROOT/runtime/caml" \
   "$HERE/noop_policy.c" -o "$HERE/noop.so"
cc -O2 -I "$ROOT/runtime" -I "$ROOT/runtime/caml" \
   "$DIR/decisionbench.c" -ldl -o "$HERE/decisionbench"

echo "==> H1: tier characterisation (membench)"
"$HERE/membench" "$FAR_DEVICE" 512 > "$RESULTS/membench.csv"

echo "==> F3: per-decision cost (microbenchmark)"
"$HERE/decisionbench" "$HERE/noop.so" > "$RESULTS/decision.csv"

echo "==> probing per-benchmark footprints (unbounded all_dram)"
for bench in $BENCHES; do
  fp=$(BENCH_SIZE="$SIZE" CAML_GC_POLICY=all_dram "$DIR/$bench" \
       | awk -F, '{print $6 + $7}')
  eval "fp_$bench=$fp"
  echo "    $bench: $fp words" >&2
done

echo "==> placement sweep (policies x benchmarks x caps x $REPS reps)"
ncell=$(( $(echo "$BENCHES" | wc -w) * $(echo "$POLICIES" | wc -w) \
          * $(echo "$FRACTIONS" | wc -w) ))
cell=0
{
  "$DIR/$(echo "$BENCHES" | cut -d' ' -f1)" --header
  for bench in $BENCHES; do
    eval "fp=\$fp_$bench"
    for policy in $POLICIES; do
      for frac in $FRACTIONS; do
        cap=$(( fp * frac / 100 ))
        cell=$((cell + 1))
        printf '[%d/%d] %s %s %d%% (cap=%d) x%s\n' \
          "$cell" "$ncell" "$bench" "$policy" "$frac" "$cap" "$REPS" >&2
        rep=0
        while [ "$rep" -lt "$REPS" ]; do
          BENCH_SIZE="$SIZE" BENCH_REP="$rep" \
          CAML_GC_POLICY="$policy" CAML_DRAM_CAP_WORDS="$cap" \
            "$DIR/$bench"
          rep=$((rep + 1))
        done
      done
    done
  done
} > "$RESULTS/placement.csv"

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
