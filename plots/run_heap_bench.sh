#!/usr/bin/env bash
# Compile and run heap_bench.ml against the local OCaml build.
# Measures minor-GC promotion latency to DRAM and FAR major heaps.
# Paste the output into plots/ as heap_results.tsv, then:
#   uv run main.py heap_results.tsv
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
BENCH="$REPO/testsuite/tests/tiered-heap/heap_bench.ml"
BIN="$REPO/testsuite/tests/tiered-heap/heap_bench.exe"

echo "=== build ===" >&2
"$REPO/ocamlopt.opt" -O3 -I "$REPO/stdlib" -I "$REPO/otherlibs/unix" unix.cmxa "$BENCH" -o "$BIN"
echo "build OK" >&2

echo ""
echo "=== DRAM (OCAML_FAR_ALL unset) ===" >&2
HEAP_BENCH_HEADER=1 "$BIN"

echo "=== FAR  (OCAML_FAR_ALL=1) ===" >&2
OCAML_FAR_ALL=1 "$BIN"
