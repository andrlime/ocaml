# Example placement policies

A **placement policy** decides, for one object about to enter the major heap,
which memory tier (*arena*) should hold it: local DRAM (`CAML_ARENA_DRAM`) or
far memory (`CAML_ARENA_FAR`). The runtime supplies all the mechanism --
arenas, promotion, pointer fixup, fact gathering -- and calls into the policy
for the single decision it owns.

This directory holds standalone example policies you compile to a shared object
and load at run time. The full ABI lives in
[`runtime/caml/placement.h`](../../runtime/caml/placement.h); that header is the
source of truth, and this README is the tour.

## The contract

A policy exports one symbol:

```c
const caml_placement_policy_ops *caml_placement_policy_entry(void);
```

It returns a pointer to a statically-lived descriptor whose only required field
is `choose_arena`:

```c
int choose_arena(const caml_placement_features *features);
```

`choose_arena` **must be a pure function** of its `features` argument plus
immutable config captured in `init`. It runs *mid-collection*, concurrently
across domains, while the heap is in an inconsistent intermediate state, so it
must:

- not allocate, and not re-enter the OCaml runtime;
- keep no mutable state -- no `static` it writes, no non-reentrant libc.

Because it is pure, it needs no locks: concurrent calls have nothing to race
on. History, if a policy wants it, comes for free from the `arena_used[]`
snapshot the engine puts in every `features` (see `dram_budget` below) -- the
policy reads it and maintains nothing.

A bad return value is harmless: the engine clamps anything out of range to the
default tier, so a buggy policy degrades placement quality, never heap
integrity.

### Optional callbacks

| Field | When | Notes |
|---|---|---|
| `init(config)` | once, at load | parse `CAML_GC_POLICY="...:<config>"`; return 0 on success |
| `after_minor(stats)` | once per minor GC | telemetry only, single-threaded; **may** do I/O |
| `shutdown()` | at exit | release anything `init` acquired |
| `should_migrate(...)` | reserved | leave `NULL` |

All callbacks except `choose_arena` may be `NULL`.

## The examples

| File | What it shows |
|---|---|
| [`string_far.c`](string_far.c) | the minimal policy: switch on `tag`, route flat bulk (strings, floats, custom blocks) to far memory, everything else to DRAM. Just `choose_arena`. |
| [`dram_budget.c`](dram_budget.c) | a richer policy: parse a byte budget in `init`, steer on the `arena_used[]` snapshot to keep DRAM under that budget, and log occupancy each collection via `after_minor`. |

## Build → load workflow

The OCaml program you run is **byte-for-byte identical** regardless of policy;
the policy is chosen at process start by an environment variable, decoupled from
compilation.

```sh
# 1. Build the policies (uses caml/placement.h from this runtime tree).
make                       # -> string_far.so, dram_budget.so

# 2. Compile your OCaml program normally -- nothing policy-specific.
ocamlopt myprog.ml -o myprog

# 3. Load a policy by pointing CAML_GC_POLICY at its path.
CAML_GC_POLICY=$PWD/string_far.so ./myprog
CAML_GC_POLICY=$PWD/dram_budget.so:1073741824 ./myprog   # 1 GiB DRAM budget
```

`CAML_GC_POLICY` accepts either a built-in name (`all_dram`, `all_far`,
`flat_far`, `size_threshold:<words>`) or a path to a `.so` (anything containing
`/` or ending in `.so`). Unset, it defaults to `all_dram`, which behaves like
stock OCaml.

To watch the runtime resolve a policy, enable GC-debug logging:

```sh
OCAMLRUNPARAM=v=0x800 CAML_GC_POLICY=$PWD/string_far.so ./myprog
# [..] placement: installed policy 'string_far'
```

If a `.so` fails to load -- missing file, no entry symbol, or an ABI mismatch --
the runtime logs the reason and falls back to `all_dram`, so the program still
runs correctly.

## Compatibility

`abi_version` must equal `CAML_PLACEMENT_ABI_VERSION`. The ABI is append-only:
new feature fields are added before the struct's reserved padding and never move
existing offsets, and a new `CAML_FEAT_` flag is just a new bit an old policy
leaves unset. `abi_version` is bumped only by a layout-breaking change, after
which the runtime rejects a stale `.so` instead of crashing.
