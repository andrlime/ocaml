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

#define CAML_INTERNALS

#include <stdlib.h>
#include <string.h>
#include "caml/camlatomic.h"
#include "caml/gc.h"
#include "caml/minor_gc.h"
#include "caml/misc.h"
#include "caml/mlvalues.h"
#include "caml/osdeps.h"
#include "caml/shared_heap.h"
#include "caml/placement_internal.h"

/* Installed once by caml_placement_init before any domain spawns, then only
   read, so a plain pointer needs no synchronisation. */
static const caml_placement_policy_ops *active_policy = NULL;

/* True iff [active_policy] supplies [after_minor]. Gates the per-collection
   accounting so a policy that ignores telemetry pays nothing for it. */
static int after_minor_active = 0;

/* Per-collection telemetry, summed across domains as they promote and read
   (then reset) by the one domain that fires [after_minor]. */
static atomic_uintnat pending_minor_words;
static atomic_uintnat pending_promoted_words;

/* The default tier: where objects live when a policy expresses no preference,
   and the backstop a buggy policy is clamped to. Tier 0 is local DRAM. */
#define CAML_ARENA_DEFAULT CAML_ARENA_DRAM

/* Map a policy's raw return into a safe arena selector: CAML_ARENA_STAY is
   honoured, anything else out of range falls back to the default tier so that
   a buggy policy can never route an object to a nonexistent arena. */
static int clamp_arena(int arena)
{
  if (arena == CAML_ARENA_STAY) return arena;
  if (arena < 0 || arena >= CAML_ARENA_COUNT) return CAML_ARENA_DEFAULT;
  return arena;
}

/* Built-in policies. */

static int all_dram_choose_arena(const caml_placement_features *features)
{
  (void) features;
  return CAML_ARENA_DRAM;
}

static const caml_placement_policy_ops all_dram_policy = {
  .abi_version     = CAML_PLACEMENT_ABI_VERSION,
  .name            = "all_dram",
  .features_needed = CAML_FEAT_NONE,
  .init            = NULL,
  .choose_arena    = all_dram_choose_arena,
  .after_minor     = NULL,
  .should_migrate  = NULL,
  .shutdown        = NULL,
};

/* Place pointer-bearing objects (low memory-level parallelism, latency
   sensitive) in DRAM and flat objects (strings, doubles, custom blocks) in far
   memory. The scannable bit is a free Tier-0 proxy for this distinction. */
static int flat_far_choose_arena(const caml_placement_features *features)
{
  return features->scannable ? CAML_ARENA_DRAM : CAML_ARENA_FAR;
}

static const caml_placement_policy_ops flat_far_policy = {
  .abi_version     = CAML_PLACEMENT_ABI_VERSION,
  .name            = "flat_far",
  .features_needed = CAML_FEAT_NONE,
  .init            = NULL,
  .choose_arena    = flat_far_choose_arena,
  .after_minor     = NULL,
  .should_migrate  = NULL,
  .shutdown        = NULL,
};

/* Place every object in far memory. Mainly an evaluation control: the
   far-memory upper bound against which steering policies are measured. */
static int all_far_choose_arena(const caml_placement_features *features)
{
  (void) features;
  return CAML_ARENA_FAR;
}

static const caml_placement_policy_ops all_far_policy = {
  .abi_version     = CAML_PLACEMENT_ABI_VERSION,
  .name            = "all_far",
  .features_needed = CAML_FEAT_NONE,
  .init            = NULL,
  .choose_arena    = all_far_choose_arena,
  .after_minor     = NULL,
  .should_migrate  = NULL,
  .shutdown        = NULL,
};

/* Place objects of at least [size_threshold_words] words in far memory and
   smaller ones in DRAM, sending bulk data to the slow tier. The threshold is
   read once at init from CAML_GC_POLICY="size_threshold:<words>" and only read
   afterwards, so it is immutable during the concurrent choose_arena calls. */
#define SIZE_THRESHOLD_DEFAULT_WORDS 256
static uintnat size_threshold_words = SIZE_THRESHOLD_DEFAULT_WORDS;

static int size_threshold_init(const char *config)
{
  if (config != NULL && config[0] != '\0') {
    uintnat words = (uintnat) strtoull(config, NULL, 0);
    if (words > 0) size_threshold_words = words;
  }
  return 0;
}

static int size_threshold_choose_arena(const caml_placement_features *features)
{
  return features->wosize >= size_threshold_words
       ? CAML_ARENA_FAR : CAML_ARENA_DRAM;
}

static const caml_placement_policy_ops size_threshold_policy = {
  .abi_version     = CAML_PLACEMENT_ABI_VERSION,
  .name            = "size_threshold",
  .features_needed = CAML_FEAT_NONE,
  .init            = size_threshold_init,
  .choose_arena    = size_threshold_choose_arena,
  .after_minor     = NULL,
  .should_migrate  = NULL,
  .shutdown        = NULL,
};

static const caml_placement_policy_ops * const builtin_policies[] = {
  &all_dram_policy,
  &all_far_policy,
  &flat_far_policy,
  &size_threshold_policy,
  NULL
};

static const caml_placement_policy_ops *find_builtin(const char *name)
{
  for (int i = 0; builtin_policies[i] != NULL; i++) {
    if (strcmp(builtin_policies[i]->name, name) == 0)
      return builtin_policies[i];
  }
  return NULL;
}

/* A spec naming a file -- rather than a built-in -- is a dynamic policy,
   loaded by dlopen. */
static int looks_like_dynamic(const char *spec)
{
  size_t len = strlen(spec);
  if (strchr(spec, '/') != NULL) return 1;
  return len >= 3 && strcmp(spec + len - 3, ".so") == 0;
}

/* Load a policy from the shared object at [path]: resolve the entry symbol,
   fetch its descriptor, and reject an ABI mismatch. Returns NULL (logging the
   reason) on any failure, so the caller falls back to the default policy. The
   library is left mapped for the process lifetime, since the descriptor it
   returns lives inside it. */
static const caml_placement_policy_ops *load_dynamic(const char *path)
{
  char_os *path_os = caml_stat_strdup_to_os(path);
  void *handle = caml_dlopen(path_os, 0);
  caml_stat_free(path_os);
  if (handle == NULL) {
    caml_gc_log("placement: cannot load '%s': %s", path, caml_dlerror());
    return NULL;
  }

  caml_placement_entry_fn entry =
    (caml_placement_entry_fn)caml_dlsym(handle, "caml_placement_policy_entry");
  if (entry == NULL) {
    caml_gc_log("placement: '%s' exports no caml_placement_policy_entry", path);
    return NULL;
  }

  const caml_placement_policy_ops *ops = entry();
  if (ops == NULL || ops->choose_arena == NULL) {
    caml_gc_log("placement: '%s' returned no usable policy", path);
    return NULL;
  }
  if (ops->abi_version != CAML_PLACEMENT_ABI_VERSION) {
    caml_gc_log("placement: '%s' is ABI v%u, runtime is v%u",
                path, ops->abi_version, (unsigned)CAML_PLACEMENT_ABI_VERSION);
    return NULL;
  }
  return ops;
}

static void install(const caml_placement_policy_ops *ops)
{
  active_policy = ops;
  after_minor_active = ops->after_minor != NULL;
  caml_gc_log("placement: installed policy '%s'", ops->name);
}

void caml_placement_init(void)
{
  const caml_placement_policy_ops *ops = &all_dram_policy;
  char_os *spec_os = caml_secure_getenv(T("CAML_GC_POLICY"));

  if (spec_os != NULL) {
    /* CAML_GC_POLICY is "name" or "name:config"; the config string, if any, is
       handed to the policy's init. */
    const char *config = NULL;
    char *spec = caml_stat_strdup_of_os(spec_os);
    char *colon = strchr(spec, ':');
    if (colon != NULL) {
      *colon = '\0';
      config = colon + 1;
    }

    if (looks_like_dynamic(spec)) {
      const caml_placement_policy_ops *loaded = load_dynamic(spec);
      if (loaded != NULL)
        ops = loaded;
      else
        caml_gc_log("placement: using '%s'", ops->name);
    } else {
      const caml_placement_policy_ops *found = find_builtin(spec);
      if (found != NULL)
        ops = found;
      else
        caml_gc_log("placement: unknown policy '%s'; using '%s'",
                    spec, ops->name);
    }

    if (ops->init != NULL && ops->init(config) != 0) {
      caml_gc_log("placement: policy '%s' init failed; using '%s'",
                  ops->name, all_dram_policy.name);
      ops = &all_dram_policy;
    }
    caml_stat_free(spec);
  }

  install(ops);
}

void caml_placement_shutdown(void)
{
  if (active_policy != NULL && active_policy->shutdown != NULL)
    active_policy->shutdown();
  active_policy = NULL;
}

void caml_placement_fill(caml_placement_features *features, header_t hd,
                         caml_domain_state *d, uint8_t site)
{
  memset(features, 0, sizeof *features);
  features->wosize = Wosize_hd(hd);
  features->tag = Tag_hd(hd);
  features->scannable = Scannable_tag(features->tag);
  features->hint = Reserved_hd(hd);
  features->site = site;
  features->color = (uint8_t)(Color_hd(hd) >> HEADER_COLOR_SHIFT);
  features->domain = d->id;
  features->minor_used = (uintnat)(d->young_end - d->young_ptr);
  features->minor_size = d->minor_heap_wsz;
  caml_shared_arena_used(features->arena_used);
}

int caml_placement_choose(const caml_placement_features *features)
{
  int arena = clamp_arena(active_policy->choose_arena(features));
  return arena == CAML_ARENA_STAY ? CAML_ARENA_DEFAULT : arena;
}

value *caml_placement_alloc(struct caml_heap_state *heap, caml_domain_state *d,
                            mlsize_t wosize, tag_t tag, reserved_t reserved,
                            uint8_t site)
{
  header_t hd = Make_header_with_reserved(wosize, tag,
                                          caml_allocation_status(), reserved);
  caml_placement_features features;
  caml_placement_fill(&features, hd, d, site);
  int arena = caml_placement_choose(&features);
  return caml_shared_try_alloc_arena(heap, wosize, tag, reserved, arena);
}

void caml_placement_record_minor(uintnat minor_words, uintnat promoted_words)
{
  if (!after_minor_active) return;
  atomic_fetch_add(&pending_minor_words, minor_words);
  atomic_fetch_add(&pending_promoted_words, promoted_words);
}

void caml_placement_after_minor(void)
{
  if (!after_minor_active) return;

  caml_placement_minor_stats stats;
  memset(&stats, 0, sizeof stats);
  stats.minor_words = atomic_exchange(&pending_minor_words, 0);
  stats.promoted_words = atomic_exchange(&pending_promoted_words, 0);
  /* promoted_blocks has no cheap source yet; left zero. */
  stats.collection = atomic_load_relaxed(&caml_minor_collections_count);
  caml_shared_arena_used(stats.arena_used);
  caml_shared_arena_capacity(stats.arena_capacity);
  active_policy->after_minor(&stats);
}
