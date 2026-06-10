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

# Sweep policies x benchmarks x DRAM caps and emit one CSV on stdout.
#
# Knobs (environment):
#   POLICIES   space-separated policy specs        (default: the built-ins)
#   BENCHES    benchmark binaries to run           (default: all three)
#   CAPS       DRAM caps in words; 0 = unlimited   (default: 0)
#   REPS       repetitions per cell                (default: 10)
#   SIZE       working-set size passed as BENCH_SIZE
#   CAML_FAR_DEVICE   devdax device for the far arena (default: runtime's)
#
# Pin the run for stable numbers, e.g.:
#   numactl --cpunodebind=0 --membind=0 sh run.sh > results/placement.csv

set -eu

DIR=$(dirname "$0")/benchmarks
POLICIES=${POLICIES:-"all_dram all_far flat_far size_threshold:256 attribute"}
BENCHES=${BENCHES:-"ptrchase arraytraverse mixed"}
CAPS=${CAPS:-0}
REPS=${REPS:-10}
SIZE=${SIZE:-2000000}

# Count cells so progress can show how far along we are.
ncap=$(echo "$CAPS" | wc -w)
nbench=$(echo "$BENCHES" | wc -w)
npol=$(echo "$POLICIES" | wc -w)
total=$((nbench * npol * ncap))
cell=0

# Header once, from any benchmark.
first=$(echo "$BENCHES" | cut -d' ' -f1)
"$DIR/$first" --header

for bench in $BENCHES; do
  for policy in $POLICIES; do
    for cap in $CAPS; do
      cell=$((cell + 1))
      printf '[%d/%d] %s %s cap=%s x%s reps\n' \
        "$cell" "$total" "$bench" "$policy" "$cap" "$REPS" >&2
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
