/**************************************************************************/
/*                                                                        */
/*                                 OCaml                                  */
/*                                                                        */
/*                               Andrew Li                                */
/*                                                                        */
/*   Copyright 2026 Andrew Li                                             */
/*                                                                        */
/*   All rights reserved.  This file is distributed under the terms of    */
/*   the GNU Lesser General Public License version 2.1, with the          */
/*   special exception on linking described in the file LICENSE.          */
/*                                                                        */
/**************************************************************************/

/* Programmable object placement: the shared ABI between the OCaml runtime
   (the "engine") and a placement policy (a built-in or a dlopen'd .so).

   The engine owns all mechanism: when GC runs, arena allocation, promotion,
   pointer fixup, and the gathering of the facts below. The policy owns one
   decision -- given the features of a single object, which memory tier (arena)
   should hold it. Calls go one direction only, engine -> policy.

   [choose_arena] is a pure, non-allocating function of its [features] argument
   plus immutable config captured at [init]. It is invoked mid-collection,
   concurrently across domains, while the heap is in an inconsistent state, so
   it must not allocate, re-enter the runtime, or keep mutable state. History,
   if wanted, is read from the [arena_used] snapshot the engine supplies.

   The ABI is append-only: new feature fields are added before [_reserved] and
   never move existing offsets; a new [CAML_FEAT_] flag is just a new bit that
   old policies leave unset. [abi_version] is bumped only by a layout-breaking
   change (reordering, removing, or retyping a field, or changing a callback
   signature), letting the engine reject a stale .so instead of crashing. */

#ifndef CAML_PLACEMENT_H
#define CAML_PLACEMENT_H

#include <stdint.h>
#include "mlvalues.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAML_PLACEMENT_ABI_VERSION 1

/* Arena ids: the value [choose_arena] returns. [CAML_ARENA_STAY] declines to
   place the object; the engine then keeps its default arena. */
enum {
  CAML_ARENA_STAY  = -1,
  CAML_ARENA_DRAM  = 0,   /* local DRAM */
  CAML_ARENA_FAR   = 1,   /* far / tiered memory */
  CAML_ARENA_COUNT = 2
};

/* Compiler placement hint, read from an object's reserved header bits
   (feature [hint]). Set by the [@main_memory] / [@far_memory] attributes. */
enum {
  CAML_HINT_NONE = 0,
  CAML_HINT_MAIN = 1,
  CAML_HINT_FAR  = 2
};

/* Where in the runtime the decision is being taken (feature [site]). */
enum {
  CAML_PLACE_PROMOTION = 0,   /* minor-to-major promotion */
  CAML_PLACE_DIRECT    = 1    /* direct major-heap allocation */
};

/* Bits in [features_needed]: which non-free facts a policy wants the engine to
   compute. A policy that leaves a bit unset pays nothing for that fact. */
typedef uint32_t caml_feature_mask;
enum {
  CAML_FEAT_NONE       = 0,
  CAML_FEAT_STRUCTURE  = 1u << 0,   /* fanout, chain_depth */
  CAML_FEAT_WRITES     = 1u << 1,   /* write_count */
  CAML_FEAT_ALLOC_SITE = 1u << 2,   /* alloc_site */
  CAML_FEAT_HOTNESS    = 1u << 3,   /* access_sample */
  CAML_FEAT_XARENA     = 1u << 4    /* xarena_refs */
};

/* The facts about one object, filled by the engine on its C stack and passed
   to [choose_arena] by const pointer. Tier-0 fields are always populated;
   higher-tier fields are gated by [features_needed] and read 0 when not
   requested or not yet implemented. */
typedef struct caml_placement_features {
  /* Tier 0 -- free facts the engine already has in hand. */
  mlsize_t wosize;             /* size in words */
  tag_t    tag;                /* object tag, 0-255 */
  uint8_t  scannable;          /* 1 if pointer-bearing, 0 if flat (MLP proxy) */
  uint8_t  hint;               /* CAML_HINT_*, from reserved header bits */
  uint8_t  site;               /* CAML_PLACE_* */
  uint8_t  color;              /* GC color bits */
  int      domain;             /* allocating / promoting domain id */
  uintnat  minor_used;         /* words used in the minor heap this cycle */
  uintnat  minor_size;         /* minor heap size in words */
  uintnat  arena_used[CAML_ARENA_COUNT];  /* live words per arena (snapshot) */

  /* Tier 1 -- structural facts (CAML_FEAT_STRUCTURE). */
  mlsize_t fanout;             /* number of pointer fields */
  uint16_t chain_depth;        /* DFS depth in the promotion graph */
  uintnat  live_bytes;         /* bytes promoted so far this collection */

  /* Tier 2 -- instrumented facts. */
  uint32_t write_count;        /* observed writes (CAML_FEAT_WRITES) */
  uint32_t alloc_site;         /* alloc-site token (CAML_FEAT_ALLOC_SITE) */

  /* Tier 3 -- sampled facts. */
  uint32_t access_sample;      /* sampled read hotness (CAML_FEAT_HOTNESS) */
  uint32_t xarena_refs;        /* cross-arena referrers (CAML_FEAT_XARENA) */

  uintnat  _reserved[4];       /* ABI growth headroom */
} caml_placement_features;

/* Per-minor-collection summary, passed to [after_minor]. Telemetry only. */
typedef struct caml_placement_minor_stats {
  uintnat promoted_words;
  uintnat promoted_blocks;
  uintnat minor_words;
  uintnat arena_used[CAML_ARENA_COUNT];
  uintnat arena_capacity[CAML_ARENA_COUNT];
  uintnat collection;          /* minor collection number */
} caml_placement_minor_stats;

/* The policy descriptor. A policy supplies this table; the engine only reads
   it. All callbacks except [choose_arena] may be NULL. Callbacks returning int
   use 0 for success. */
typedef struct caml_placement_policy_ops {
  uint32_t          abi_version;     /* must equal CAML_PLACEMENT_ABI_VERSION */
  const char       *name;
  caml_feature_mask features_needed;
  int  (*init)(const char *config);
  int  (*choose_arena)(const caml_placement_features *);  /* required, pure */
  void (*after_minor)(const caml_placement_minor_stats *);
  int  (*should_migrate)(const caml_placement_features *, int);  /* reserved */
  void (*shutdown)(void);
} caml_placement_policy_ops;

/* A dlopen'd policy .so exports this symbol; it returns a pointer to a
   statically-lived descriptor. */
typedef const caml_placement_policy_ops *(*caml_placement_entry_fn)(void);
const caml_placement_policy_ops *caml_placement_policy_entry(void);

#ifdef __cplusplus
}
#endif

#endif /* CAML_PLACEMENT_H */
