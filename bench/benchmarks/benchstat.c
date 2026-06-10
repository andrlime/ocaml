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

#include <time.h>
#include <caml/mlvalues.h>
#include <caml/alloc.h>
#include <caml/shared_heap.h>
#include <caml/placement.h>
#include <caml/placement_internal.h>
#include <caml/far_arena.h>

/* Stats stub the benchmarks link against to read the placement engine's view
 * of a run: per-arena committed backing (the occupancy that proves steering)
 * and the choose_arena decision count (the per-decision overhead denominator).
 * GC counts and wall time come from the OCaml Stdlib side. */

CAMLprim value bench_arena_committed(value arena)
{
  uintnat used[CAML_ARENA_COUNT];
  caml_shared_arena_used(used);
  return Val_long(used[Long_val(arena)]);
}

CAMLprim value bench_decision_count(value unit)
{
  (void) unit;
  return Val_long(caml_placement_decision_count());
}

CAMLprim value bench_far_available(value unit)
{
  (void) unit;
  return Val_bool(caml_far_arena_available());
}

/* Monotonic wall-clock nanoseconds, as a float (we never need sub-ns and a
 * float dodges boxing an int64 across the FFI). */
CAMLprim value bench_now_ns(value unit)
{
  struct timespec t;
  (void) unit;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return caml_copy_double(t.tv_sec * 1e9 + t.tv_nsec);
}
