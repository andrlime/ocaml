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
 * A decision is a call to the policy's choose_arena. We time it three ways over
 * a huge loop so the per-call cost is isolated from any noise:
 *   - inlinable: a direct call the compiler can inline (the floor),
 *   - indirect:  through a function pointer, as the built-in dispatch does,
 *   - dlopen:    through a pointer into a dlopen'd .so, the loadable path.
 * The point is that even the loadable path is a couple of nanoseconds, so
 * programmability costs essentially nothing per object.
 *
 * Build: cc -O2 -I <runtime> -I <runtime>/caml decisionbench.c -ldl -o ...
 * Run:   ./decisionbench [policy.so]      (default ./noop.so)
 * Output: CSV "path,ns_per_call" on stdout.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "caml/placement.h"

#define ITERS 500000000UL

/* Same trivial body as the noop .so policy, so the three paths differ only in
 * how the call is made, not in what it computes. */
static int local_choose(const caml_placement_features *f)
{
  (void) f;
  return CAML_ARENA_DRAM;
}

static double now_ns(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e9 + t.tv_nsec;
}

int main(int argc, char **argv)
{
  const char *so = argc > 1 ? argv[1] : "./noop.so";
  caml_placement_features f;
  volatile long sink = 0;
  double t0, t1;

  memset(&f, 0, sizeof f);
  printf("path,ns_per_call\n");

  t0 = now_ns();
  for (unsigned long i = 0; i < ITERS; i++) {
    f.wosize = i; sink += local_choose(&f);
  }
  t1 = now_ns();
  printf("inlinable,%.3f\n", (t1 - t0) / ITERS);

  int (*volatile fp)(const caml_placement_features *) = local_choose;
  t0 = now_ns();
  for (unsigned long i = 0; i < ITERS; i++) { f.wosize = i; sink += fp(&f); }
  t1 = now_ns();
  printf("indirect,%.3f\n", (t1 - t0) / ITERS);

  void *h = dlopen(so, RTLD_NOW);
  if (h != NULL) {
    caml_placement_entry_fn entry =
      (caml_placement_entry_fn) dlsym(h, "caml_placement_policy_entry");
    const caml_placement_policy_ops *ops = entry();
    int (*cf)(const caml_placement_features *) = ops->choose_arena;
    t0 = now_ns();
    for (unsigned long i = 0; i < ITERS; i++) { f.wosize = i; sink += cf(&f); }
    t1 = now_ns();
    printf("dlopen,%.3f\n", (t1 - t0) / ITERS);
  } else {
    fprintf(stderr, "decisionbench: cannot dlopen '%s'\n", so);
  }

  (void) sink;
  return 0;
}
