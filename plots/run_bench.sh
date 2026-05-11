#!/usr/bin/env bash
# Run this script yourself and paste the output back.
# It compiles write_bw.c and runs the benchmark against both DRAM and /dev/dax2.0.
set -e
cd "$(dirname "$0")"

echo "=== build ==="
gcc -O2 -mclwb -o write_bw ../testsuite/tests/tiered-heap/write_bw.c -lm -lnuma
echo "build OK"

echo ""
echo "=== benchmark ==="
./write_bw
