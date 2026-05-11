/**************************************************************************/
/*                                                                        */
/*                                 OCaml                                  */
/*                                                                        */
/*                          Tiered major heap                             */
/*                                                                        */
/**************************************************************************/

/* DAX-backed pool allocator for the second (far-memory) major-heap arena.
 *
 * On systems with an Intel Optane / CXL-attached devdax character device
 * (default /dev/dax2.0), this arena mmaps a contiguous region from the device
 * once and hands out 32 KiB pools (matching POOL_WSIZE * sizeof(value)) and
 * larger bump-allocated chunks for >SIZECLASS_MAX allocations.
 *
 * Devdax constraints relevant here:
 *   - MAP_SHARED is mandatory (MAP_PRIVATE silently COWs into anonymous DRAM).
 *   - The mmap base address is 2 MiB aligned by the kernel.
 *   - There is no filesystem and no msync(); we don't need persistence.
 *
 * Initialisation is lazy and idempotent. If the device is missing or
 * inaccessible the arena marks itself permanently unavailable; subsequent
 * acquisition requests return NULL.
 */

#ifndef CAML_DAX_ARENA_H
#define CAML_DAX_ARENA_H

#ifdef CAML_INTERNALS

#include <stddef.h>
#include "config.h"

/* Idempotent. Reads OCAML_DAX_DEVICE (default "/dev/dax2.0") and
 * OCAML_DAX_BYTES (default 8 GiB, rounded up to the device alignment).
 * On failure marks the arena unavailable and logs a single line. */
extern void caml_dax_arena_init(void);

/* 1 if the DAX arena successfully initialised, 0 otherwise. */
extern int caml_dax_arena_available(void);

/* Acquire / release a pool of Bsize_wsize(POOL_WSIZE) bytes.  The returned
 * pointer is at least 32 KiB aligned (in fact 2 MiB aligned at the start,
 * pool-aligned thereafter). Returns NULL if the arena is exhausted or
 * unavailable. caml_dax_pool_release returns the pool to the arena's free
 * list; the memory is never returned to the OS. */
extern void* caml_dax_pool_acquire(void);
extern void  caml_dax_pool_release(void* pool);

/* Bump-pointer large allocation inside the DAX region. Returns a 64-byte
 * aligned pointer into the device, or NULL on exhaustion. The bump cursor
 * is monotonic; freed regions are not reclaimed (acceptable for Week 4
 * scaffolding; benchmarking workloads should not rely on this). */
extern void* caml_dax_large_alloc(size_t bytes);

/* Returns 1 if [p] points anywhere inside the DAX-mapped region, 0 otherwise.
 * Always returns 0 if the arena is unavailable. Safe to call before init. */
extern int caml_dax_contains(const void* p);

/* For testing: returns the mapped base address and total byte count.
 * Both are 0 if the arena is unavailable. */
extern void caml_dax_arena_extent(void** base_out, size_t* bytes_out);

#endif /* CAML_INTERNALS */

#endif /* CAML_DAX_ARENA_H */
