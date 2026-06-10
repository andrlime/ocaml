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

/* dram_budget -- keep DRAM under a byte budget, spill the rest to far memory.
 *
 * Demonstrates the three things a richer policy uses beyond choose_arena:
 *   - init:        capture immutable config from CAML_GC_POLICY="...:<bytes>"
 *   - arena_used:  read the live-words snapshot the engine hands to every
 *                  call, so the policy steers on history while keeping no
 *                  mutable state of its own
 *   - after_minor: a per-collection telemetry hook (fired once, single-
 *                  threaded) that may log -- unlike choose_arena, it is free
 *                  to do I/O
 *
 * Build:  make dram_budget.so
 * Load:   CAML_GC_POLICY=$PWD/dram_budget.so:1073741824 ./prog   (1 GiB)
 */

#include "caml/placement.h"
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_BUDGET_WORDS ((uintnat)256 * 1024 * 1024 / sizeof(value))

/* Set once in init, then only read by the concurrent choose_arena calls. */
static uintnat budget_words = DEFAULT_BUDGET_WORDS;

static int init(const char *config)
{
  if (config != NULL && config[0] != '\0') {
    unsigned long long bytes = strtoull(config, NULL, 0);
    if (bytes > 0) budget_words = (uintnat)(bytes / sizeof(value));
  }
  return 0;
}

static int choose_arena(const caml_placement_features *f)
{
  return f->arena_used[CAML_ARENA_DRAM] < budget_words
       ? CAML_ARENA_DRAM : CAML_ARENA_FAR;
}

static void after_minor(const caml_placement_minor_stats *s)
{
  fprintf(stderr,
          "[dram_budget] collection %lu: dram=%lu far=%lu words "
          "(budget %lu)\n",
          (unsigned long)s->collection,
          (unsigned long)s->arena_used[CAML_ARENA_DRAM],
          (unsigned long)s->arena_used[CAML_ARENA_FAR],
          (unsigned long)budget_words);
}

static const caml_placement_policy_ops policy = {
  .abi_version  = CAML_PLACEMENT_ABI_VERSION,
  .name         = "dram_budget",
  .init         = init,
  .choose_arena = choose_arena,
  .after_minor  = after_minor,
};

const caml_placement_policy_ops *caml_placement_policy_entry(void)
{
  return &policy;
}
