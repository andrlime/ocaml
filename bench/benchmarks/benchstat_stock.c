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

/* Timing-only variant of benchstat.c for building the benchmarks against a
 * stock OCaml compiler that has no placement engine. It provides the same
 * externals so bench_common.ml links unchanged, but the placement-specific
 * ones return zero -- only the wall clock is real. Used by overhead_vs_stock.sh
 * to get the framework-free baseline (figure F2). */

#include <time.h>
#include <caml/mlvalues.h>
#include <caml/alloc.h>

CAMLprim value bench_arena_committed(value arena)
{
  (void) arena;
  return Val_long(0);
}

CAMLprim value bench_decision_count(value unit)
{
  (void) unit;
  return Val_long(0);
}

CAMLprim value bench_far_available(value unit)
{
  (void) unit;
  return Val_bool(0);
}

CAMLprim value bench_now_ns(value unit)
{
  struct timespec t;
  (void) unit;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return caml_copy_double(t.tv_sec * 1e9 + t.tv_nsec);
}
