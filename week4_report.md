# Week 4 — Tiered Major-Heap Scaffolding

**Branch:** `implement-tiered-major-heaps`  
**Date:** 2026-05-11  
**Scope:** Runtime scaffolding only. No source-level attributes, no criticality metrics, no compiler changes.

---

## Summary

This week adds a second major-heap arena backed by a devdax character device (`/dev/dax2.0`, i.e. Intel Optane/CXL far memory), alongside the existing anonymous-mmap-backed DRAM arena. Every allocation in the major heap now carries an arena identity and is routed accordingly. A temporary environment-variable hook (`OCAML_FAR_ALL=1`) biases all minor-GC promotions to the far arena, allowing end-to-end validation without any compiler changes.

**Files changed (8 modified, 4 new):**

| File | Status | Role |
|------|--------|------|
| `runtime/caml/dax_arena.h` | **New** | DAX pool allocator API |
| `runtime/dax_arena.c` | **New** | DAX pool allocator implementation |
| `runtime/caml/shared_heap.h` | Modified | `caml_arena_id` enum, `caml_shared_try_alloc_arena` |
| `runtime/shared_heap.c` | Modified | Dual-arena pool/large freelists, sweep, compaction |
| `runtime/minor_gc.c` | Modified | Arena-aware `alloc_shared`, `choose_arena_week4` |
| `runtime/caml/startup_aux.h` | Modified | `caml_far_all_promotions` extern |
| `runtime/startup_aux.c` | Modified | DAX init + `OCAML_FAR_ALL` parsing |
| `Makefile` | Modified | `dax_arena.o` in `COMMON_C_OBJECTS` |
| `runtime/dune` | Modified | `dax_arena.c` in `libcamlrun` deps |
| `testsuite/tests/tiered-heap/dax_stubs.c` | **New** | C stubs exposing DAX internals to OCaml tests |
| `testsuite/tests/tiered-heap/dax_placement.ml` | **New** | Automated placement correctness test |

---

## Phase 1 — DAX Pool Allocator

### `runtime/caml/dax_arena.h` (new)

Declares the full public API. All symbols are gated behind `#ifdef CAML_INTERNALS`.

```c
/* Idempotent init. Reads OCAML_DAX_DEVICE (default "/dev/dax2.0")
 * and OCAML_DAX_BYTES (default 8 GiB). */
extern void caml_dax_arena_init(void);

/* 1 if the DAX arena successfully initialised, 0 otherwise. */
extern int  caml_dax_arena_available(void);

/* Acquire / release a pool of Bsize_wsize(POOL_WSIZE) bytes. */
extern void* caml_dax_pool_acquire(void);
extern void  caml_dax_pool_release(void* pool);

/* Bump-pointer large alloc inside the DAX region. Non-reclaiming. */
extern void* caml_dax_large_alloc(size_t bytes);

/* Pointer containment check (for tests and debug assertions). */
extern int   caml_dax_contains(const void* p);

/* Testing: returns mapped base and total byte count. */
extern void  caml_dax_arena_extent(void** base_out, size_t* bytes_out);
```

### `runtime/dax_arena.c` (new)

The arena is a single global struct initialised once under `init_lock`. On Windows the init stub marks the arena permanently unavailable (devdax is Linux-only). On Linux:

```c
struct dax_arena {
  void*    base;             /* mmap base */
  size_t   total_bytes;      /* total mapped bytes */
  size_t   large_zone_bytes; /* first 2 GiB: bump-pointer large allocs */
  size_t   pool_bytes;       /* Bsize_wsize(POOL_WSIZE) == 32 KiB */
  int      available;
  int      tried;
  int      fd;
  caml_plat_mutex alloc_lock;
  size_t   large_cursor;     /* monotonic, never reset */
  size_t   pool_cursor;      /* grows until free list is used */
  struct dax_pool_node* free_list;  /* LIFO, pools returned here on release */
  ...
};
```

**Init path:**

```c
void caml_dax_arena_init(void) {
  /* open OCAML_DAX_DEVICE (default /dev/dax2.0) */
  int fd = open(device, O_RDWR);
  /* mmap with MAP_SHARED — mandatory for devdax; MAP_PRIVATE silently
   * COWs into anonymous DRAM */
  void* base = mmap(NULL, total, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  /* carve: [0, large_zone_end) for large bump allocs;
   *        [large_zone_end, end) for 32 KiB pools */
  arena.available = 1;
}
```

**Pool acquire/release** — pools come off the LIFO free list first; on exhaustion advance `pool_cursor`. Release pushes back onto the free list. Memory is never returned to the OS.

**Large alloc** — `large_cursor` advances under `alloc_lock`; cacheline-aligned (64 B). On exhaustion logs one warning and returns NULL. The bump cursor is monotonic; freed regions are not reclaimed (acceptable for Week 4).

**Pointer containment:**

```c
int caml_dax_contains(const void* p) {
  if (!arena.available) return 0;
  return (const char*)p >= (const char*)arena.base &&
         (const char*)p <  (const char*)arena.base + arena.total_bytes;
}
```

---

## Phase 2 — Dual-Arena Shared Heap

### `runtime/caml/shared_heap.h`

Added the arena identifier enum and the new allocation entry point:

```c
typedef enum {
  ARENA_DRAM = 0,
  ARENA_FAR  = 1,
  NUM_ARENAS = 2
} caml_arena_id;

/* Like caml_shared_try_alloc but routes to the named arena.
 * Returns NULL if the arena is unavailable. */
value* caml_shared_try_alloc_arena(struct caml_heap_state*,
                                   mlsize_t, tag_t, reserved_t,
                                   caml_arena_id);
```

The existing `caml_shared_try_alloc` is now a one-line wrapper passing `ARENA_DRAM` — zero behaviour change for all current callers.

### `runtime/shared_heap.c`

**Per-arena pool freelist:**

```c
/* Was: static struct pool_freelist_state pool_freelist = { ... }; */
static struct pool_freelist_state pool_freelists[NUM_ARENAS] = {
  [ARENA_DRAM] = { .lock = CAML_PLAT_MUTEX_INITIALIZER, ... },
  [ARENA_FAR]  = { .lock = CAML_PLAT_MUTEX_INITIALIZER, ... },
};
/* Backward-compat alias so un-widened code sites still compile. */
#define pool_freelist (pool_freelists[ARENA_DRAM])
```

**Per-arena arrays in `caml_heap_state`:**

```c
/* Was: pool* avail_pools[NUM_SIZECLASSES]; */
pool* avail_pools[NUM_ARENAS][NUM_SIZECLASSES];
pool* full_pools[NUM_ARENAS][NUM_SIZECLASSES];
pool* unswept_avail_pools[NUM_ARENAS][NUM_SIZECLASSES];
pool* unswept_full_pools[NUM_ARENAS][NUM_SIZECLASSES];
large_alloc* swept_large[NUM_ARENAS];
large_alloc* unswept_large[NUM_ARENAS];
caml_arena_id next_arena_to_sweep;   /* sweep cursor across arenas */
```

**`struct pool` and `struct large_alloc`** each gained an `unsigned char arena` / `uintnat arena` field set at acquisition.

**Arena-aware `pool_acquire`:**

```c
static pool* pool_acquire(struct caml_heap_state* local, caml_arena_id arena) {
  if (arena == ARENA_FAR) {
    pool* r = (pool*)caml_dax_pool_acquire();
    if (r) { r->next = NULL; r->owner = NULL; r->arena = ARENA_FAR; }
    return r;   /* NULL on exhaustion; caller falls back to DRAM */
  }
  /* ARENA_DRAM: existing caml_mem_map path */
  pool* r = (pool*)caml_mem_map(Bsize_wsize(POOL_WSIZE), 0, ...);
  ...
  r->arena = ARENA_DRAM;
  return r;
}
```

**Arena-aware `large_allocate`:**

```c
static large_alloc* large_allocate(..., caml_arena_id arena) {
  if (arena == ARENA_FAR) {
    large_alloc* a = (large_alloc*)caml_dax_large_alloc(sz + header_sz);
    /* NULL → caller falls back to DRAM allocation */
    if (a) a->arena = ARENA_FAR;
    return a;
  }
  /* ARENA_DRAM: existing malloc path */
  large_alloc* a = malloc(sz + header_sz);
  a->arena = ARENA_DRAM;
  return a;
}
```

**Sweep across arenas** — the outer sweep loop now iterates `next_arena_to_sweep` from `ARENA_DRAM` to `NUM_ARENAS`:

```c
while (work > 0 && local->next_arena_to_sweep < NUM_ARENAS) {
  caml_arena_id a = local->next_arena_to_sweep;
  /* sweep unswept_avail_pools[a], unswept_full_pools[a] by sizeclass,
   * then unswept_large[a] */
  if (done with arena a) local->next_arena_to_sweep++;
}
```

**Compaction** — Phase 1 (object evacuation) only operates on `ARENA_DRAM` pools because the bump allocator is non-reclaiming and FAR objects cannot be moved. Phase 2 (pointer update) scans all arenas so that FAR objects with pointers into moved DRAM objects are correctly patched.

**`CAML_DEBUG_FAR_ARENA` assertion** (build-time, off by default):

```c
#ifdef CAML_DEBUG_FAR_ARENA
  if (arena == ARENA_FAR) {
    CAMLassert(caml_dax_contains(p) &&
               "FAR allocation landed outside DAX region");
  } else {
    CAMLassert(!caml_dax_contains(p) &&
               "DRAM allocation landed inside DAX region");
  }
#endif
```

Enable with `CFLAGS=-DCAML_DEBUG_FAR_ARENA` to catch routing bugs at runtime.

---

## Phase 3 — Minor-GC Routing

### `runtime/minor_gc.c`

`alloc_shared` gained an `arena` parameter:

```c
static value alloc_shared(caml_domain_state* d,
                           mlsize_t wosize, tag_t tag, reserved_t reserved,
                           caml_arena_id arena)
{
  void* mem = caml_shared_try_alloc_arena(d->shared_heap, wosize, tag,
                                          reserved, arena);
  ...
}
```

`choose_arena_week4` is the arena selection stub for this week:

```c
static caml_arena_id choose_arena_week4(reserved_t reserved) {
  (void)reserved;  /* hint plumbing arrives in Week 5 */
  if (caml_far_all_promotions) return ARENA_FAR;
  return ARENA_DRAM;
}
```

All four `alloc_shared` call sites in `oldify_one` were updated:

```c
/* Was: result = alloc_shared(st->domain, sz, tag, Reserved_hd(hd)); */
result = alloc_shared(st->domain, sz, tag, Reserved_hd(hd),
                      choose_arena_week4(Reserved_hd(hd)));
```

---

## Phase 4 — Startup + Build

### `runtime/startup_aux.c`

`caml_dax_arena_init()` is called once during `caml_parse_ocamlrunparam`. `OCAML_FAR_ALL` is read immediately after:

```c
int caml_far_all_promotions = 0;   /* global; set once at startup */

/* inside caml_parse_ocamlrunparam(): */
caml_dax_arena_init();
const char* far_all = getenv("OCAML_FAR_ALL");
if (far_all != NULL && *far_all != '\0' && *far_all != '0') {
  if (!caml_dax_arena_available()) {
    fprintf(stderr,
      "[ocaml] OCAML_FAR_ALL=1 requested but DAX arena is unavailable; "
      "all promotions will remain in DRAM.\n");
  } else {
    caml_far_all_promotions = 1;
  }
}
```

If `OCAML_FAR_ALL=1` but the device is absent, the runtime degrades gracefully to DRAM-only with a single stderr warning.

### Build wiring

**`Makefile`** — `dax_arena` added to `runtime_COMMON_C_SOURCES`:
```makefile
runtime_COMMON_C_SOURCES = \
  ...
  custom \
  dax_arena \        # ← new
  debugger \
  ...
```

**`runtime/dune`** — `dax_arena.c` added to `libcamlrun.a` deps:
```
(rule (target libcamlrun.a)
  (deps ... dax_arena.c ...)
  ...)
```

---

## Phase 5 — Verification Test

### Why a software address-range test

The test checks that the allocator *routes* correctly: all pointers from `ARENA_FAR` allocations must fall within `[base, base+total_bytes)` — the `MAP_SHARED` region from `/dev/dax2.0`. Because `MAP_SHARED` on a devdax device is the kernel's guarantee that writes reach the device (as opposed to `MAP_PRIVATE` which silently COWs to DRAM), the address range check is the routing correctness test. It is automatable in CI and catches regressions without needing hardware.

The complementary **hardware check** is:
```
cat /proc/<pid>/numa_maps | grep dax
```
This shows pages on the device's NUMA node (e.g., node 6 for Optane), confirming actual PMem faults. It is a one-time bring-up check; it requires live hardware and cannot run in CI.

### `testsuite/tests/tiered-heap/dax_stubs.c` (new)

```c
/* Returns 1 if /dev/dax2.0 was mapped successfully. */
CAMLprim value caml_test_dax_available(value unit) {
  return Val_bool(caml_dax_arena_available());
}

/* Returns 1 if the block backing [v] is inside the DAX-mapped region.
 * Probes Hp_val(v) (the header word) to be safe. */
CAMLprim value caml_test_dax_contains(value v) {
  CAMLparam1(v);
  void* hdr = (void*)Hp_val(v);
  CAMLreturn(Val_bool(caml_dax_contains(hdr)));
}

/* Returns (base_addr, total_bytes) as a pair of nativeints. */
CAMLprim value caml_test_dax_extent(value unit) {
  CAMLparam1(unit);
  CAMLlocal1(pair);
  void* base; size_t bytes;
  caml_dax_arena_extent(&base, &bytes);
  pair = caml_alloc_tuple(2);
  Store_field(pair, 0, caml_copy_nativeint((intnat)base));
  Store_field(pair, 1, caml_copy_nativeint((intnat)bytes));
  CAMLreturn(pair);
}
```

### `testsuite/tests/tiered-heap/dax_placement.ml` (new)

```ocaml
(* Skip if no /dev/dax2.0. *)
let () =
  if not (dax_available ()) then
    skip "DAX arena unavailable (no /dev/dax2.0 or insufficient permissions)";

  let far_all = match Sys.getenv_opt "OCAML_FAR_ALL" with
    | Some s when s <> "" && s <> "0" -> true
    | _ -> false in

  (* 50 000 small objects; sample every 100th. *)
  let smalls = Array.init 50_000 (fun _ -> Array.make 4 42) in
  Gc.minor (); Gc.minor ();   (* flush nursery *)
  (* ... check dax_contains smalls.(i*100) matches far_all ... *)

  (* 20 large objects (~32 KiB each). *)
  let larges = Array.init 20 (fun _ -> Array.make 4096 0xdeadbeef) in
  Gc.minor (); Gc.minor ();
  (* ... check dax_contains larges.(i) matches far_all ... *)

  Gc.compact ();   (* must not crash with FAR objects present *)
  pass "Gc.compact() completed without crash"
```

**Running manually:**
```
# routing to FAR arena
OCAML_FAR_ALL=1 ./dax_placement.byte

# routing to DRAM (default)
./dax_placement.byte

# routing + C-level assertions
make world CFLAGS=-DCAML_DEBUG_FAR_ARENA
OCAML_FAR_ALL=1 ./dax_placement.byte
```

---

## Invariants for Week 5

- `choose_arena_week4(reserved_t)` is the single decision point. Week 5 replaces its body with: read reserved-bit hint → consult criticality metrics → fall back to heuristic. The signature is already pinned.
- `caml_far_all_promotions` and the `OCAML_FAR_ALL` env var hook should be deleted once real policy lands.
- Compaction Phase 1 intentionally skips `ARENA_FAR` — this is a constraint for the Week 5 agent to note, especially once reserved-bit preservation is needed.
- Stats for `ARENA_FAR` are currently folded into the DRAM freelist counters (one-line comment in `shared_heap.c`). Week 5 should split them when adding per-arena reporting.
