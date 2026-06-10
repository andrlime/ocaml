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

#include <string.h>
#include "caml/misc.h"
#include "caml/osdeps.h"
#include "caml/placement_internal.h"

/* Installed once by caml_placement_init before any domain spawns, then only
   read, so a plain pointer needs no synchronisation. */
static const caml_placement_policy_ops *active_policy = NULL;

/* Map a policy's raw return into a safe arena selector: CAML_ARENA_STAY is
   honoured, anything else out of range falls back to local DRAM so that a
   buggy policy can never route an object to a nonexistent arena. */
static int clamp_arena(int arena)
{
  if (arena == CAML_ARENA_STAY) return arena;
  if (arena < 0 || arena >= CAML_ARENA_COUNT) return CAML_ARENA_DRAM;
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

static const caml_placement_policy_ops * const builtin_policies[] = {
  &all_dram_policy,
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
   loaded by dlopen (not yet implemented; see Phase 3.1). */
static int looks_like_dynamic(const char *spec)
{
  size_t len = strlen(spec);
  if (strchr(spec, '/') != NULL) return 1;
  return len >= 3 && strcmp(spec + len - 3, ".so") == 0;
}

static void install(const caml_placement_policy_ops *ops)
{
  active_policy = ops;
  caml_gc_log("placement: installed policy '%s'", ops->name);
}

void caml_placement_init(void)
{
  const caml_placement_policy_ops *ops = &all_dram_policy;
  char_os *spec_os = caml_secure_getenv(T("CAML_GC_POLICY"));

  if (spec_os != NULL) {
    char *spec = caml_stat_strdup_of_os(spec_os);
    if (looks_like_dynamic(spec)) {
      caml_gc_log("placement: dynamic policy '%s' ignored "
                  "(dlopen support not yet built); using '%s'",
                  spec, ops->name);
    } else {
      const caml_placement_policy_ops *found = find_builtin(spec);
      if (found != NULL)
        ops = found;
      else
        caml_gc_log("placement: unknown policy '%s'; using '%s'",
                    spec, ops->name);
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

int caml_placement_choose(const caml_placement_features *features)
{
  return clamp_arena(active_policy->choose_arena(features));
}
