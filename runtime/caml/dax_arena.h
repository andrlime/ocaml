/**************************************************************************/
/*                                                                        */
/*                                 OCaml                                  */
/*                                                                        */
/*                          Tiered major heap                             */
/*                                                                        */
/**************************************************************************/

/* DAX-backed pool allocator for the far-memory major-heap arena.
 * MAP_SHARED is mandatory on devdax — MAP_PRIVATE silently COWs to DRAM. */

#ifndef CAML_DAX_ARENA_H
#define CAML_DAX_ARENA_H

#ifdef CAML_INTERNALS

#include <stddef.h>
#include "config.h"

/* Idempotent init. Reads OCAML_DAX_DEVICE (default /dev/dax2.0) and
 * OCAML_DAX_BYTES (default 8 GiB). Marks arena unavailable on any error. */
extern void caml_dax_arena_init(void);

extern int   caml_dax_arena_available(void);

/* Acquire/release a POOL_WSIZE pool. Returns NULL on exhaustion. Released
 * pools go on a LIFO free list; memory is never returned to the OS. */
extern void* caml_dax_pool_acquire(void);
extern void  caml_dax_pool_release(void* pool);

/* Bump-pointer large alloc, 64-byte aligned. Non-reclaiming; returns NULL
 * on exhaustion. */
extern void* caml_dax_large_alloc(size_t bytes);

/* 1 if p is inside the DAX-mapped region, 0 otherwise. */
extern int   caml_dax_contains(const void* p);

/* Returns mapped base and total byte count (both 0 if unavailable). */
extern void  caml_dax_arena_extent(void** base_out, size_t* bytes_out);

#endif /* CAML_INTERNALS */

#endif /* CAML_DAX_ARENA_H */
