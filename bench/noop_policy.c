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

/* A dlopen policy identical in behaviour to the built-in all_dram: every object
 * goes to DRAM. Paired with the static all_dram, it isolates the cost of the
 * dlopen indirect call per decision (figure F3). */

#include "caml/placement.h"

static int choose_arena(const caml_placement_features *f)
{
  (void) f;
  return CAML_ARENA_DRAM;
}

static const caml_placement_policy_ops policy = {
  .abi_version  = CAML_PLACEMENT_ABI_VERSION,
  .name         = "noop",
  .choose_arena = choose_arena,
};

const caml_placement_policy_ops *caml_placement_policy_entry(void)
{
  return &policy;
}
