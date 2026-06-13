# Programmable Criticality-Aware Object Placement in OCaml 5

A framework for **programmable garbage-collector object placement across tiered
memory** (local DRAM + far memory). Think *`sched_ext` for GC placement*: the
runtime supplies all mechanism (arenas, promotion, pointer fixup, fact
collection); a user-supplied **policy** supplies only judgment — given facts
about one object, which memory tier should it live in.

---

## 1. Thesis & framing

The contribution is the **framework**, not the discovery of one winning
criticality heuristic. This deliberately sidesteps the three weaknesses of the
original proposal:

1. *Criticality metrics are hard to engineer* — so we don't bet the project on
   any one of them; the engine exposes cheap facts and the **policy** decides.
2. *Good heuristics are hard to find* — so the headline result is
   **expressiveness + overhead** (the framework can host many policies at
   near-zero cost), not "our heuristic wins."
3. *The prior attempt was sloppy/unchecked* — so **every commit builds and
   passes its test**, and the public contract is frozen in one reviewed header.

**Evaluation story:** (a) expressiveness — N distinct policies expressible;
(b) overhead — the framework's per-decision cost is near zero; (c) policy
spread — placement is demonstrably steered, and some policy beats the all-far
control.

---

## 2. Architecture: strict mechanism/policy split

| The **engine** (runtime) owns | The **policy** (user `.so` / built-in) owns |
|---|---|
| When GC runs, arena `mmap`/`mbind`, pool freelists | The decision: `features → arena` |
| Promotion atomicity, pointer fixup, thread coordination | Nothing else — it touches no heap, allocates nothing |
| Computing facts; routing the object; **defending correctness** | Being a pure function |

**Calls go one direction only: engine → policy.** The policy is a passive
callback table; it may never re-enter the runtime.

### The communication mechanism

Same address space (the policy `.so` is `dlopen`'d in), identical struct layout
guaranteed by a **single shared header** (`runtime/caml/placement.h`). The wire
is a plain C function call:

```
ENGINE  ──fills caml_placement_features on its C stack──▶  choose_arena(const features*)
ENGINE  ◀──────────────── returns int arena id ─────────────────────────────────
```

No serialization, no IPC, no copy (passed by `const` pointer). The only thing
flowing back is the arena id. A one-time back-channel — `features_needed`, read
at `init` — tells the engine which non-free facts are worth computing.

### Interaction lifecycle

```
START:  read CAML_GC_POLICY → resolve → ops.init(config); read ops.features_needed
RUN:    per promoted/allocated object →  ops.choose_arena(&features)   [hot path]
        per minor collection          →  ops.after_minor(&stats)       [optional, unused in scope]
END:    ops.shutdown()
```

### The policy contract: a pure function

> **`choose_arena` is a pure function of its `features` argument plus
> immutable config from `init`. No mutable state, no domain-local scratch, no
> atomics.**

This is the entire contract. Consequences:

- **No concurrency protection needed.** `choose_arena` runs concurrently across
  domains during STW minor GC, but a pure function under concurrent calls has
  nothing to race on — no locks, no reentrancy hedging. (The only obligation is
  *genuine* purity: no hidden `static` mutable state, no non-reentrant libc.)
- **History, if a policy wants it, comes for free** from the aggregate snapshot
  the engine already puts in `features` (`arena_used[]`). The policy reads it; it
  maintains nothing.
- **Determinism is *not* a correctness requirement.** Under the promotion race
  (`minor_gc.c:210`), multiple domains may speculatively place the same object;
  the CAS picks one arbitrary winner and losers abandon their copies — the heap
  is correct regardless of which arena the winner chose. A pure function of the
  object's *immutable* features happens to be deterministic too, which is nice
  for reproducibility, but it is not load-bearing for integrity.

### Why policies are C, not OCaml

`choose_arena` fires **mid-collection**, while the heap is in an inconsistent
intermediate state (forwarding pointers, `In_progress` headers, the minor heap
being emptied). Running OCaml there would allocate and could trigger a nested
GC → corruption. So the runtime callback **must be non-allocating C**. OCaml
*authoring* is still available without that hazard:

- **Manual placement** — programmers write plain OCaml with `[@far_memory]` /
  `[@main_memory]` attributes (Phase 4); the GC reads the baked-in hint.
- **(Deferred) expr DSL** — a small OCaml-flavored pure expression compiled at
  `init` and evaluated by the C engine over `features`.

Real-OCaml callbacks are explicitly **not** pursued.

> **Naming note:** the domain state already has `caml_gc_policy` — OCaml's
> major-heap *allocation* policy (next-fit/best-fit). It is unrelated. We use
> `caml_placement_*` everywhere to avoid the collision.

---

## 3. The data model (`runtime/caml/placement.h`)

Three structs, two directions. **Tier 0 is populated in scope (Phases 1–4);
Tiers 1–3 are present-but-zero, gated by `features_needed`, enabled later with
no ABI bump.**

### Engine → policy: `caml_placement_features` (per object)

| # | Field | Type | Meaning | Source | Tier |
|---|---|---|---|---|---|
| 1 | `wosize` | `mlsize_t` | size in words | `Wosize_hd(hd)` `mlvalues.h:162` | 0 |
| 2 | `tag` | `tag_t` | object tag 0–255 | `Tag_hd(hd)` `mlvalues.h:160` | 0 |
| 3 | `scannable` | `uint8_t` | 1=pointer-bearing (low MLP), 0=flat/`No_scan` (high MLP) — **free MLP proxy** | `Scannable_tag(tag)` `mlvalues.h:253` | 0 |
| 4 | `hint` | `uint8_t` | compiler attribute `CAML_HINT_NONE/MAIN/FAR` | `Reserved_hd(hd)` `mlvalues.h:172` | 0 |
| 5 | `site` | `uint8_t` | `CAML_PLACE_PROMOTION` vs `CAML_PLACE_DIRECT` | caller | 0 |
| 6 | `color` | `uint8_t` | GC status/color bits | `Color_hd(hd)` `mlvalues.h:184` | 0 |
| 7 | `domain` | `int` | allocating/promoting domain id | `d->id` | 0 |
| 8 | `minor_used` | `uintnat` | words used in minor heap this cycle | `young_end − young_ptr` | 0 |
| 9 | `minor_size` | `uintnat` | minor heap size in words | `d->minor_heap_wsz` | 0 |
| 10 | `arena_used[2]` | `uintnat[]` | live words per arena (aggregate-history snapshot) | engine accounting | 0 |
| 11 | `fanout` | `mlsize_t` | number of pointer fields | `wosize` of scannable | 1 |
| 12 | `chain_depth` | `uint16_t` | DFS depth in promotion graph | counter in `oldify_one` | 1 |
| 13 | `live_bytes` | `uintnat` | bytes promoted so far this collection | `st->live_bytes` `minor_gc.c:295` | 1 |
| 14 | `write_count` | `uint32_t` | writes observed (0 if N/A) | `caml_modify` + side table | 2 |
| 15 | `alloc_site` | `uint32_t` | allocation-site provenance (0 if N/A) | compiler token | 2 |
| 16 | `access_sample` | `uint32_t` | sampled read hotness (0 if N/A) | PEBS sampler | 3 |
| 17 | `xarena_refs` | `uint32_t` | cross-arena referrers (0 if N/A) | cross-arena rem-set | 3 |
| — | `_reserved[4]` | `uintnat[]` | ABI growth headroom | — | — |

> **`tag` (field 2) classes** a policy will switch on: `String_tag` /
> `Double_tag` / `Double_array_tag` / `Custom_tag` (flat bulk, far-friendly);
> `Closure_tag` / `Cont_tag` (code/fibers); `Object_tag`; ordinary
> tuples/records/variants (`tag < No_scan_tag`, scannable).

### Engine → policy: `caml_placement_minor_stats` (per minor GC)

`promoted_words`, `promoted_blocks`, `minor_words`, `arena_used[2]`,
`arena_capacity[2]`, `collection`. (Delivered to `after_minor`, NULL-able;
unused in scope but useful for Phase 9 telemetry.)

### Policy → engine: `caml_placement_policy_ops` (the descriptor / "vtable")

```c
typedef struct caml_placement_policy_ops {
  uint32_t          abi_version;     /* == CAML_PLACEMENT_ABI_VERSION */
  const char       *name;
  caml_feature_mask features_needed;
  int  (*init)(const char *config);                              /* NULL ok; 0 = success */
  int  (*choose_arena)(const caml_placement_features *);         /* REQUIRED, pure */
  void (*after_minor)(const caml_placement_minor_stats *);       /* NULL ok */
  int  (*should_migrate)(const caml_placement_features *, int);  /* NULL ok; reserved */
  void (*shutdown)(void);                                        /* NULL ok */
} caml_placement_policy_ops;
```

A dlopen'd policy `.so` exports:
`const caml_placement_policy_ops *caml_placement_policy_entry(void);`

### Constants

- Arenas: `CAML_ARENA_DRAM=0`, `CAML_ARENA_FAR=1`, `CAML_ARENA_COUNT=2`,
  `CAML_ARENA_STAY=-1`.
- Hints: `CAML_HINT_NONE=0`, `CAML_HINT_MAIN=1`, `CAML_HINT_FAR=2`.
- Feature flags: `CAML_FEAT_NONE`, `CAML_FEAT_STRUCTURE`, `CAML_FEAT_WRITES`,
  `CAML_FEAT_ALLOC_SITE`, `CAML_FEAT_HOTNESS`, `CAML_FEAT_XARENA`.
- `CAML_PLACEMENT_ABI_VERSION = 1`.

### ABI / compatibility rules

- **Append-only**: new fields go before `_reserved`; existing offsets never
  move. An old `.so` reads a prefix; new fields sit past where it looks.
- **A new `FEAT_` flag is just a new bit**: old policies don't set it → engine
  skips the work → they pay nothing.
- **No `abi_version` bump** for adding fields or flags. Bump **only** on a
  layout-breaking change (reorder/remove/retype a field, or change a callback
  signature). The load-time version check then refuses a stale `.so` with a
  clear error instead of crashing.

---

## 4. Registration & loading

`CAML_GC_POLICY` is resolved at GC init, **before domains spawn**:

- contains `/` or ends in `.so` → `dlopen` + `dlsym("caml_placement_policy_entry")`
  → check `abi_version` → `init` → install.
- otherwise → look the name up in the static **built-in** table.
- unset → `all_dram` (single-arena; behaves like stock OCaml — the safe baseline).

The active `ops` pointer is stored in a read-mostly global, set once, read
lock-free on the hot path. The user's compiled OCaml program is byte-for-byte
identical regardless of policy — loading happens in the runtime at process
start, decoupled from both compile steps. A static-link variant of a policy
exists only for the overhead-floor benchmark.

### Adding a new feature (extensibility recipe)

There is **one** place the engine fills the struct:
`caml_placement_fill(features*, header_t hd, caml_domain_state* d, site)` in
`placement.c`, called from both fill sites.

- **Free feature** (engine-known, e.g. `arena_capacity`): add the field
  (append-only) + one line in `caml_placement_fill`. No flag, no subsystem.
- **Instrumented feature** (Tier 2/3): add field + a `CAML_FEAT_` flag; build
  the generation+storage subsystem (e.g. `caml_modify` bump → address-keyed
  side table → lookup in `caml_placement_fill`), all guarded by the flag so
  non-users pay one well-predicted branch. The branch checks a global bool
  cached at `init`.

### How Tiers 1–3 are obtained (when enabled later)

| Feature | Generation point | Storage | On hot path? |
|---|---|---|---|
| `chain_depth` (T1) | `oldify_one` recursion | depth param threaded through walk | no (gated) |
| `write_count` (T2) | `caml_modify` `memory.c:210` | minor-scoped address→count side table | no |
| `alloc_site` (T2) | compiler-emitted token | reserved bits / side word | no |
| `access_sample` (T3) | **PEBS** (`perf mem` / `perf_event_open` w/ `PERF_SAMPLE_ADDR`+`WEIGHT`), **not** Intel PT — PT traces control flow, not data addresses | background sampler thread → object/arena hotness table | no (async) |
| `xarena_refs` (T3) | `caml_modify` cross-arena rem-set | rem-set | no |

Hotness (PEBS) is statistical, off the hot path, needs kernel perf perms, and
is primarily useful for **migration** (far→near), which is deferred.

---

## 5. Commit-by-commit roadmap

**Scope: Phases 0–4 + 9.** Legend: 🟥 risky (review hard, run full testsuite) ·
🟩 safe. **Every commit builds and passes its stated test before merge.**
Branch: `programmable-placement`.

### Phase 0 — Contract & scaffolding (no behavior change)

- **0.1** 🟩 **Add `runtime/caml/placement.h`** — the full data model in §3
  (structs, enums, `FEAT_*` flags, arena/hint constants, ABI version, contract
  doc comments). Header only, no users.
  *Files:* `runtime/caml/placement.h`.
  *Test:* compiles standalone (`cc -fsyntax-only`) and in-tree.
- **0.2** 🟩 **Add `runtime/placement.c` + internal header** — read-mostly active
  policy global, `clamp_arena`, the built-in `all_dram` descriptor, a static
  built-in table, and `caml_placement_init()` / `caml_placement_shutdown()`
  recognizing built-in **names** only (dlopen stubbed). Wire init/shutdown into
  runtime startup. `choose_arena` callable but result ignored (no arenas yet).
  *Files:* `runtime/placement.c`, `runtime/caml/placement_internal.h`,
  `runtime/startup_nat.c` / `runtime/startup_byt.c`, `runtime/Makefile`, `dune`.
  *Test:* runtime boots; `CAML_GC_POLICY=all_dram` installs the policy
  (debug log / assert).

### Phase 1 — Two arenas (the foundation)

- **1.1** 🟥 **Introduce `caml_arena` abstraction; route the existing single heap
  through arena 0.** Behavior-preserving refactor of `shared_heap.c` pool
  acquisition so all current allocation flows through `arena[CAML_ARENA_DRAM]`.
  *Files:* `runtime/shared_heap.c`, `runtime/caml/shared_heap.h`.
  *Test:* **full testsuite green** (this is the risky refactor — no behavior
  change permitted).
- **1.2** 🟩 **Add arena 1 (far) backing + arena-aware allocation entry point.**
  `mmap` arena 1 and `mbind` it to `CAML_FAR_NUMA_NODE` (fallback to plain
  `mmap` if libnuma absent / no second node). Add
  `caml_shared_try_alloc_arena(heap, wosize, tag, reserved, arena_id)`.
  Arena 1 still unused by promotion.
  *Files:* `runtime/shared_heap.c`, `runtime/caml/shared_heap.h`,
  `configure.ac`/`Makefile.config` (optional libnuma probe).
  *Test:* a debug hook allocates into arena 1; `move_pages`/`numastat` confirms
  pages land on the target node (or fallback path taken cleanly).
- **1.3** 🟩 **Per-arena accounting.** Track `arena_used`/`arena_capacity` per
  arena; expose for the features struct (field 10) and stats. Document the
  experimental NUMA-latency setup (emulated-CXL latency comes from VM topology,
  not runtime code).
  *Files:* `runtime/shared_heap.c`, `docs/` or `PLAN.md` appendix.
  *Test:* accounting matches a known allocation workload.

### Phase 2 — Wire policy into placement (Tier-0 features only — MVP core)

- **2.1** 🟥 **Promotion path: fill Tier-0 `features`, call `choose_arena`, route.**
  Add `caml_placement_fill` (the single fill site); in `alloc_shared`
  (`minor_gc.c:152`) fill Tier 0, call the policy, route to
  `caml_shared_try_alloc_arena`. **Fallback to the other arena if the chosen one
  is full** (preserve the no-fail invariant at `minor_gc.c:160`). With
  `all_dram` ⇒ behavior identical to baseline.
  *Files:* `runtime/minor_gc.c`, `runtime/placement.c`.
  *Test:* `all_dram` byte-identical behavior; a deterministic test policy
  (flat→far / pointer→DRAM) places objects as asserted (verify via accounting).
- **2.2** 🟩 **Direct major-alloc path.** Same wiring at `caml_alloc_shr` /
  `caml_shared_try_alloc` with `site = CAML_PLACE_DIRECT`.
  *Files:* `runtime/memory.c`, `runtime/shared_heap.c`.
  *Test:* large/direct allocations honor the policy.
- **2.3** 🟩 **Built-in policies.** `all_far`, `size_threshold` (env-configured
  via `init`), and `flat_far` (scannable→DRAM, flat→FAR — the natural Tier-0
  criticality policy). All pure functions. (No `round_robin` — it would need
  hot-path state; the deterministic policies are more testable.)
  *Files:* `runtime/placement.c`.
  *Test:* placement distribution matches each policy's intent.
- **2.4** 🟩 **`after_minor` telemetry hook.** Fire `after_minor` with stats at the
  end of minor GC from a single thread at the barrier; default policy ignores.
  No mutable-state semantics — telemetry only.
  *Files:* `runtime/minor_gc.c`, `runtime/placement.c`.
  *Test:* a counting policy observes expected per-collection stats.

### Phase 3 — dlopen path (fast iteration — completes the MVP)

- **3.1** 🟩 **dlopen resolver.** In `caml_placement_init`: if `CAML_GC_POLICY`
  is a path/`.so`, `dlopen` + `dlsym("caml_placement_policy_entry")`, check
  `abi_version`, call `init`, install. Link `-ldl`.
  *Files:* `runtime/placement.c`, `runtime/Makefile`/`dune`, `configure.ac`.
  *Test:* load an external `.so`, verify placement matches it.
- **3.2** 🟩 **Examples + docs.** `examples/policies/` with 1–2 sample policies, a
  build rule (`cc -shared -fPIC policy.c -o policy.so`), and a README
  documenting the ABI and the write→compile→load workflow.
  *Files:* `examples/policies/*.c`, `examples/policies/Makefile`,
  `examples/policies/README.md`.
  *Test:* example `.so` builds and runs end-to-end.

> **✅ End of Phase 3 = minimum defensible result:** programmable placement,
> built-in + dlopen policies, measurable.

### Phase 4 — Compiler attributes (`[@far_memory]` / `[@main_memory]`)

- **4.1** 🟩 **HINT encoding vs build config.** Confirm the
  `--enable-reserved-header-bits=2` build (already configured) and pin the
  `CAML_HINT_*` ↔ reserved-bits mapping; runtime read path via `Reserved_hd`.
  *Files:* `runtime/caml/placement.h`, a runtime probe test.
  *Test:* reserved bits set in a header are readable at runtime.
- **4.2** 🟩 **Register attributes.** Add `far_memory` / `main_memory` to
  `Builtin_attributes` so they're recognized (no warning 53).
  *Files:* `utils/builtin_attributes.ml(i)`.
  *Test:* using the attributes produces no warning; misuse still warns.
- **4.3** 🟥 **Thread the hint through Lambda.** Add a `memory_hint` type to
  `lambda.ml(i)`; `get_memory_hint_attribute` in `translattribute.ml`; extend
  `Pmakeblock` to carry the hint and read `exp_attributes` in `translcore.ml`.
  **Touches every `Pmakeblock` match arm across `middle_end/`/`asmcomp/`** —
  large mechanical change.
  *Files:* `lambda/lambda.ml(i)`, `lambda/translattribute.ml`,
  `lambda/translcore.ml`, all `Pmakeblock` consumers.
  *Test:* `-dlambda` dump shows the hint on annotated allocations.
- **4.4** 🟥 **Lower the hint to the header.** Carry the hint through
  Clambda/Cmm so the allocation's `reserved` argument is set, and emit a
  reserved-bit header write for the minor-heap `Alloc_small` path.
  *Files:* `middle_end/`/`asmcomp/cmmgen.ml` (Cmm generation), allocation lowering.
  *Test:* allocate an annotated object; `Reserved_val` reads the expected
  `CAML_HINT_*` at runtime.
- **4.5** 🟩 **`attribute` built-in policy.** A policy that honors `hint`
  (`FAR`→far, `MAIN`→DRAM) and falls back to a configurable sub-policy for
  `NONE`.
  *Files:* `runtime/placement.c`, example.
  *Test:* annotated objects land where annotated; unannotated follow fallback.

### Phase 9 — Evaluation (needs ≥ Phase 3; attributed condition needs Phase 4)

- **9.1** 🟩 **Benchmarks.** Pointer-chasing (low MLP), array-traversal (high
  MLP), and a mixed workload; a memory-pressure knob (shrink the heap / cap the
  DRAM arena). Hand-written (not AI-generated, per the proposal's AI policy).
  *Files:* `bench/`.
- **9.2** 🟩 **Harness.** Run all conditions — all-dram (upper bound), all-far
  (control), `size_threshold`, `flat_far`, attributed — collecting wall-time,
  arena occupancy, and **per-decision overhead** (including a static-linked
  policy variant for the zero-cost floor).
  *Files:* `bench/run.sh`, `bench/measure.*`.
- **9.3** 🟩 **Figures.** Expressiveness table, overhead curve, policy-spread plots.
- **9.4** 🟩 **Writeup.** Results + analysis feeding the report.

---

## 6. Critical path & deferred work

```
0.1→0.2→1.1→1.2→1.3→2.1→2.2→2.3→2.4→3.1→3.2     = MVP (defensible result)
                                          └── Phase 4 (attributes) ── Phase 9 (eval)
```

- **MVP = Phases 0–3.** The whole thesis (programmable placement + overhead)
  stands on its own here.
- **Phase 4 is an independent enrichment** on top of the MVP.
- **Deferred (not in scope), ABI-ready (NULL `should_migrate`, unused flags/fields):**
  - **Phase 5** — Tier-1 structural features (`fanout`, `chain_depth`).
  - **Phase 6** — Migration: arena-aware compaction (`should_migrate` during
    `caml_compact_heap`, `shared_heap.c:1149`) + cross-arena remembered set.
    Migration is a compaction-class operation because OCaml uses direct pointers
    (no indirection) — the cost is whole-heap pointer fixup, which only
    compaction already does.
  - **Phase 7** — Tier-2/3 features (`write_count`, `alloc_site`, PEBS hotness).
  - **Phase 8** — Expr-language policy (the "policy without C" surface).

---

## 7. Build / environment

- Tree configured with **`--enable-reserved-header-bits=2`** so baseline and
  attributed conditions share one ABI (a fair comparison).
- Far memory = a NUMA node via `mbind`; emulated-CXL latency comes from the
  VM's NUMA topology, configured in the harness — **not** in runtime code.
- `libnuma` optional with graceful fallback: where there is no second node, the
  far arena maps to a second `mmap` region, so **Phases 0–4 are fully testable
  on any machine**. A NUMA-capable VM is needed only for Phase 9 numbers.

---

## 8. Top risks & mitigations

1. **1.1 single→arena refactor** — could break heap invariants.
   *Mitigation:* behavior-preserving, arena 0 only, full-testsuite gate.
2. **4.3 / 4.4 `Pmakeblock` + Cmm threading** — wide mechanical change;
   reserved-bit write on `Alloc_small` is fiddly.
   *Mitigation:* land IR (4.3) and codegen (4.4) separately; verify with
   `-dlambda` / `-dcmm` dumps and a runtime `Reserved_val` probe.
3. **Policy purity violations** — a policy with hidden static state.
   *Mitigation:* contract documented in the header; built-ins are pure; engine
   clamps return values so a bad policy degrades quality, never integrity.
4. **No NUMA hardware during development.**
   *Mitigation:* fallback mapping keeps Phases 0–4 testable; real node only for
   Phase 9.

---

## 9. Decisions log (resolved during design)

- **Framework, not heuristic** — contribution is expressiveness + overhead.
- **Policy = one pure C function** (`choose_arena`); no state, no concurrency
  protection, history via the `arena_used[]` snapshot.
- **No OCaml policy callbacks** — unsafe mid-collection; OCaml authoring is via
  attributes (Phase 4) and the deferred expr DSL.
- **Registration** via `CAML_GC_POLICY` env var → dlopen `.so` or built-in name.
- **Determinism is not required for correctness** — only reproducibility.
- **Hotness uses PEBS, not Intel PT** (PT traces control flow, not data
  addresses); deferred and off the hot path.
- **`caml_placement_*` naming** to avoid the existing `caml_gc_policy` field.
