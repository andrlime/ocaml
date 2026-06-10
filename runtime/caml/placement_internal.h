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

/* Engine-internal interface to the placement subsystem. The cross-ABI types
   it traffics in live in the public caml/placement.h. */

#ifndef CAML_PLACEMENT_INTERNAL_H
#define CAML_PLACEMENT_INTERNAL_H

#ifdef CAML_INTERNALS

#include "placement.h"
#include "domain_state.h"

struct caml_heap_state;

/* Resolve CAML_GC_POLICY and install the active policy. Called once from
   caml_init_gc, before any domain spawns. */
void caml_placement_init(void);

/* Run the active policy's shutdown callback, if any, and forget it. */
void caml_placement_shutdown(void);

/* The single site where the engine populates [features] for one object about
   to enter the major heap: [hd] is the object's header, [d] the allocating or
   promoting domain, and [site] a CAML_PLACE_* selector. Fills the Tier-0
   facts; higher tiers are left zero until their subsystems exist. Non-
   allocating, so it is safe to call mid-collection. */
void caml_placement_fill(caml_placement_features *features, header_t hd,
                         caml_domain_state *d, uint8_t site);

/* The arena the active policy chooses for [features], resolved to a concrete
   tier ready to allocate into: an out-of-range answer or CAML_ARENA_STAY (a
   migration concept, meaningless at allocation time) becomes the default tier.
   Pure in [features]; safe to call concurrently across domains. */
int caml_placement_choose(const caml_placement_features *features);

/* Allocate a [wosize]/[tag]/[reserved] block into [heap], placed in the tier
   the active policy chooses for an object entering at [site]. This is the one
   path objects take into the major heap, from both promotion and direct major
   allocation. Returns the block's header pointer, or NULL on allocation
   failure. */
value *caml_placement_alloc(struct caml_heap_state *heap, caml_domain_state *d,
                            mlsize_t wosize, tag_t tag, reserved_t reserved,
                            uint8_t site);

#endif /* CAML_INTERNALS */

#endif /* CAML_PLACEMENT_INTERNAL_H */
