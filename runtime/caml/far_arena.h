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

/* Backing memory for the far arena (CAML_ARENA_FAR).

   Far memory is a devdax character device, mapped MAP_SHARED so pages stay on
   the device instead of copy-on-writing back to DRAM. Memory is handed out
   with a bump pointer and is never returned to the device; the shared heap
   recycles freed far pools through its own per-arena freelist.

   The device path and the number of bytes to map are read from CAML_FAR_DEVICE
   and CAML_FAR_BYTES. If the device cannot be opened or mapped the arena
   reports itself unavailable and callers fall back to DRAM, so the runtime
   still works on hosts without far memory. */

#ifndef CAML_FAR_ARENA_H
#define CAML_FAR_ARENA_H

#ifdef CAML_INTERNALS

#include "config.h"

/* Whether the far device is mapped and ready. The first call performs a
   one-time initialisation; subsequent calls are cheap. */
int caml_far_arena_available(void);

/* Carve [bytes] of far memory aligned to [align] (a power of two). Returns
   NULL if the arena is unavailable or exhausted. */
void *caml_far_arena_alloc(uintnat bytes, uintnat align);

/* Whether [p] lies within the mapped far region. */
int caml_far_arena_contains(const void *p);

#endif /* CAML_INTERNALS */

#endif /* CAML_FAR_ARENA_H */
