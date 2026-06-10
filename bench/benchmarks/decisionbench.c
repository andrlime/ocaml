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

/* Microbenchmark for figure F3: the cost of one placement decision.
 *
 * A decision is a call to a policy's choose_arena through its ops table -- the
 * exact indirection the engine performs (active_policy->choose_arena). We time
 * it two ways, identical except for where the policy code lives:
 *   - builtin: a policy compiled into this binary,
 *   - dlopen:  the same policy loaded from a .so.
 * Both go through [ops->choose_arena], so the comparison isolates the cost of
 * making a policy loadable. That the two are equal (a couple of ns) is the
 * point: programmability via dlopen costs nothing per object.
 *
 * The ops pointers are volatile so the compiler cannot devirtualise and inline
 * the call -- it must emit the indirect call the engine emits.
 *
 * Build: cc -O2 -I <runtime> -I <runtime>/caml decisionbench.c -ldl -o ...
 * Run:   ./decisionbench [policy.so] [reps]   (default ./noop.so 100)
 * Output: CSV "path,rep,ns_per_call" on stdout -- one row per repetition per
 * path, so the plot can show the spread.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "caml/placement.h"

#define ITERS 20000000UL

/* A policy compiled into this binary -- the "built-in" case. Same trivial body
 * as the noop .so policy, so the two paths differ only in code location. */
static int builtin_choose(const caml_placement_features *f)
{
  (void) f;
  return CAML_ARENA_DRAM;
}

static const caml_placement_policy_ops builtin_policy = {
  .abi_version  = CAML_PLACEMENT_ABI_VERSION,
  .name         = "builtin",
  .choose_arena = builtin_choose,
};

static double now_ns(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e9 + t.tv_nsec;
}

/* Time ITERS calls of [ops->choose_arena]. [ops] is volatile so the call is a
 * genuine indirect call, as in caml_placement_choose. */
static double time_dispatch(const caml_placement_policy_ops *volatile ops)
{
  caml_placement_features f;
  volatile long sink = 0;
  double t0, t1;
  memset(&f, 0, sizeof f);
  t0 = now_ns();
  for (unsigned long i = 0; i < ITERS; i++) {
    f.wosize = i;
    sink += ops->choose_arena(&f);
  }
  t1 = now_ns();
  (void) sink;
  return (t1 - t0) / ITERS;
}

int main(int argc, char **argv)
{
  const char *so = argc > 1 ? argv[1] : "./noop.so";
  long reps = argc > 2 ? atol(argv[2]) : 100;

  const caml_placement_policy_ops *sops = NULL;
  void *h = dlopen(so, RTLD_NOW);
  if (h != NULL) {
    caml_placement_entry_fn entry =
      (caml_placement_entry_fn) dlsym(h, "caml_placement_policy_entry");
    sops = entry();
  } else {
    fprintf(stderr, "decisionbench: cannot dlopen '%s'\n", so);
  }

  /* Interleave the two paths each rep so any drift in clock or frequency hits
   * both equally. */
  printf("path,rep,ns_per_call\n");
  for (long r = 0; r < reps; r++) {
    printf("builtin,%ld,%.3f\n", r, time_dispatch(&builtin_policy));
    if (sops != NULL)
      printf("dlopen,%ld,%.3f\n", r, time_dispatch(sops));
  }
  return 0;
}
