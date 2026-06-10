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

# Figure F2 (framework overhead vs stock). Opt-in and slower than the main
# pipeline because it builds a second, stock OCaml compiler:
#
#   1. add a git worktree at trunk and build world.opt there,
#   2. compile the benchmarks against it with the timing-only stub,
#   3. run them (stock baseline) alongside this branch under all_dram,
#   4. emit results/overhead.csv and render fig_f2_overhead.png.
#
#   sh overhead_vs_stock.sh
#
# Knobs: SIZE, REPS, FIGDIR (as in make_figures.sh), STOCK_REF (default trunk),
# WORKTREE (default ../ocaml-stock).

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
SIZE=${SIZE:-4000000}
REPS=${REPS:-10}
FIGDIR=${FIGDIR:-$HOME/figures}
RESULTS="$HERE/results"
STOCK_REF=${STOCK_REF:-trunk}
WORKTREE=${WORKTREE:-$ROOT/../ocaml-stock}

mkdir -p "$RESULTS" "$FIGDIR"

echo "==> preparing stock worktree at $WORKTREE ($STOCK_REF)"
if [ ! -d "$WORKTREE" ]; then
  git -C "$ROOT" worktree add "$WORKTREE" "$STOCK_REF"
fi
if [ ! -x "$WORKTREE/ocamlopt.opt" ]; then
  echo "==> configuring + building stock (this takes a while)"
  ( cd "$WORKTREE" \
    && ./configure --enable-reserved-header-bits=2 >/dev/null \
    && make -j"$(nproc)" world.opt >/dev/null )
fi

# Build a set of benchmarks against a given compiler root, with a given stub.
build_set() {
  root=$1; stub=$2; outdir=$3
  mkdir -p "$outdir"
  oc="$root/ocamlopt.opt -I $root/stdlib"
  $oc -I "$root/runtime" -ccopt -I"$root/runtime" -c "$HERE/benchmarks/$stub" \
     -o "$outdir/benchstat.o"
  $oc -c "$HERE/benchmarks/bench_common.ml" -o "$outdir/bench_common.cmx" \
     -I "$outdir"
  for b in ptrchase arraytraverse mixed; do
    $oc -I "$outdir" "$outdir/bench_common.cmx" "$HERE/benchmarks/$b.ml" \
       "$outdir/benchstat.o" -o "$outdir/$b"
  done
}

echo "==> building benchmarks (stock and branch/all_dram)"
build_set "$WORKTREE" benchstat_stock.c "$HERE/build-stock"
build_set "$ROOT"     benchstat.c       "$HERE/build-branch"

echo "==> running -> overhead.csv"
{
  "$HERE/build-stock/ptrchase" --header
  for b in ptrchase arraytraverse mixed; do
    r=0
    while [ "$r" -lt "$REPS" ]; do
      BENCH_SIZE="$SIZE" BENCH_REP="$r" "$HERE/build-stock/$b" \
        | sed 's/,all_dram,/,stock,/'
      BENCH_SIZE="$SIZE" BENCH_REP="$r" CAML_GC_POLICY=all_dram \
        "$HERE/build-branch/$b"
      r=$((r + 1))
    done
  done
} > "$RESULTS/overhead.csv"

echo "==> rendering fig_f2_overhead.png"
( cd "$HERE" && uv run python plot_overhead.py \
    "$RESULTS/overhead.csv" "$FIGDIR/fig_f2_overhead.png" )

echo "==> done."
