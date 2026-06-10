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

# Fast path for figure F5 (the DRAM-pressure sweep) on its own. The full
# make_figures.sh sweeps every policy x benchmark; F5 only needs the headline
# workload and the three policies that show the three distinct behaviours --
# naive-local (all_dram), naive-far (all_far), criticality-aware (flat_far). A
# smaller working set and 3 reps bring it to ~10 minutes on devdax.
#
#   sh make_f5.sh
#
# Knobs: BENCH (default mixed), SIZE, REPS, POLICIES, FRACTIONS, FAR_DEVICE,
# FIGDIR. Pin with numactl as usual.

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
FAR_DEVICE=${FAR_DEVICE:-/dev/dax1.0}
BENCH=${BENCH:-mixed}
SIZE=${SIZE:-1000000}
REPS=${REPS:-3}
POLICIES=${POLICIES:-"all_dram all_far flat_far"}
FRACTIONS=${FRACTIONS:-"100 75 55 40 28 18"}
FIGDIR=${FIGDIR:-$HOME/figures}
RESULTS="$HERE/results"
DIR="$HERE/benchmarks"

export CAML_FAR_DEVICE="$FAR_DEVICE"
mkdir -p "$RESULTS" "$FIGDIR"

echo "==> building $BENCH"
make -C "$DIR" >/dev/null

fp=$(BENCH_SIZE="$SIZE" CAML_GC_POLICY=all_dram "$DIR/$BENCH" \
     | awk -F, '{print $6 + $7}')
echo "    $BENCH footprint: $fp words" >&2

ncell=$(( $(echo "$POLICIES" | wc -w) * $(echo "$FRACTIONS" | wc -w) ))
cell=0
{
  "$DIR/$BENCH" --header
  for policy in $POLICIES; do
    for frac in $FRACTIONS; do
      cap=$(( fp * frac / 100 ))
      cell=$((cell + 1))
      printf '[%d/%d] %s %s %d%% (cap=%d) x%s\n' \
        "$cell" "$ncell" "$BENCH" "$policy" "$frac" "$cap" "$REPS" >&2
      rep=0
      while [ "$rep" -lt "$REPS" ]; do
        BENCH_SIZE="$SIZE" BENCH_REP="$rep" \
        CAML_GC_POLICY="$policy" CAML_DRAM_CAP_WORDS="$cap" \
          "$DIR/$BENCH"
        rep=$((rep + 1))
      done
    done
  done
} > "$RESULTS/pressure.csv"

echo "==> rendering $FIGDIR/fig_f5_pressure.png"
( cd "$HERE" && uv run python plot_pressure.py \
    "$RESULTS/pressure.csv" "$FIGDIR/fig_f5_pressure.png" "$BENCH" )
echo "==> done."
