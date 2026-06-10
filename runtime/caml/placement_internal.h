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

/* Resolve CAML_GC_POLICY and install the active policy. Called once from
   caml_init_gc, before any domain spawns. */
void caml_placement_init(void);

/* Run the active policy's shutdown callback, if any, and forget it. */
void caml_placement_shutdown(void);

/* Where the active policy would place an object, clamped to a valid arena
   selector. Pure in [features]; safe to call concurrently across domains. */
int caml_placement_choose(const caml_placement_features *features);

#endif /* CAML_INTERNALS */

#endif /* CAML_PLACEMENT_INTERNAL_H */
