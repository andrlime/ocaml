# Placement evaluation

Benchmarks, data collection, and figures for the programmable-placement work.
The story the figures tell, in order:

| Fig | File | Claim |
|---|---|---|
| H1 | `plot_hardware.py` | the DRAM and far tiers genuinely differ (latency + bandwidth) |
| F1 | `plot_expressiveness.py` | many policies, tiny, three authoring surfaces |
| F2 | `plot_overhead.py` | the framework is free vs stock OCaml (`all_dram`) |
| F3 | `plot_decision_cost.py` | a placement decision costs a few ns (static vs dlopen) |
| A1 | `plot_envelope.py` | `all_dram` (upper bound) vs `all_far` (naive control) |
| F4 | `plot_spread.py` | criticality-aware policies fill the envelope |
| F5 | `plot_pressure.py` | the win grows as DRAM gets scarcer |
| F6 | `plot_occupancy.py` | the steering is real: arena occupancy splits by policy |

## Layout

- `membench.c` — standalone DRAM-vs-far latency/bandwidth probe (figure H1).
- `benchmarks/` — hand-written OCaml workloads (pointer-chase, array-traverse,
  mixed) plus the C stat stub they link.
- `run.sh` — sweeps policies × benchmarks × DRAM caps, emits one CSV.
- `plot_*.py` — one script per figure; all share `plotting.py` (vanilla
  matplotlib, seaborn colours, no custom fonts).
- `example-data/` — synthetic CSVs in the real schema, so the plotters (and the
  figure layouts) can be checked without the devdax box.

## Python

Managed with **uv**. Run a plotter with:

```sh
uv run python plot_hardware.py results/membench.csv out.png
```

## Collecting real data (devdax box)

```sh
# H1: tier characterisation
gcc -O2 -o membench membench.c && ./membench /dev/dax1.0 512 > results/membench.csv

# F2-F6: placement sweep (sets CAML_GC_POLICY / CAML_DRAM_CAP_WORDS per run)
./run.sh > results/placement.csv
```

`run.sh` reads its knobs from the environment (`CAML_FAR_DEVICE`, repetition
count, DRAM-cap fractions); see the top of the script. The runtime knobs it
drives are `CAML_GC_POLICY` (built-in name or `.so` path) and
`CAML_DRAM_CAP_WORDS` (soft DRAM cap that spills to far under pressure).

## CSV schema (`results/placement.csv`)

```
bench,policy,dram_cap,rep,wall_ms,dram_live_words,far_live_words,
dram_committed,far_committed,far_promoted_words,minor_colls,major_colls,
gc_ms,decision_count
```
