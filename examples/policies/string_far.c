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

/* string_far -- a minimal placement policy.
 *
 * Routes flat bulk objects (strings, boxed floats, float arrays, custom
 * blocks) to far memory and keeps everything else, including all
 * pointer-bearing objects, in DRAM. This is the smallest useful policy: one
 * pure choose_arena and nothing else.
 *
 * Build:  make string_far.so          (see ../Makefile)
 * Load:   CAML_GC_POLICY=$PWD/string_far.so ./your_program
 */

#include "caml/placement.h"

static int choose_arena(const caml_placement_features *f)
{
  switch (f->tag) {
    case String_tag:
    case Double_tag:
    case Double_array_tag:
    case Custom_tag:
      return CAML_ARENA_FAR;
    default:
      return CAML_ARENA_DRAM;
  }
}

static const caml_placement_policy_ops policy = {
  .abi_version  = CAML_PLACEMENT_ABI_VERSION,
  .name         = "string_far",
  .choose_arena = choose_arena,
};

const caml_placement_policy_ops *caml_placement_policy_entry(void)
{
  return &policy;
}
