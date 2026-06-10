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

/* membench -- characterise the DRAM and far-memory tiers the placement engine
 * routes between. Standalone hardware tool (not part of the runtime): it maps a
 * region in each tier and measures sequential read/write bandwidth and random
 * dependent-load latency, so the evaluation can show the tiers genuinely
 * differ. The far tier is a devdax device mapped MAP_SHARED, the same way
 * runtime/far_arena.c maps it.
 *
 * Build:  gcc -O2 -o membench membench.c
 * Run:    ./membench [far_device] [size_mib]      (default /dev/dax1.0 512)
 * Output: CSV "tier,metric,value,unit" on stdout; notes on stderr.
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define CACHELINE   64UL
#define DAX_ALIGN   (2UL * 1024 * 1024)   /* devdax 2 MiB mapping alignment */

static double now_ns(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e9 + t.tv_nsec;
}

static size_t align_up(size_t x, size_t a) { return (x + a - 1) & ~(a - 1); }

/* Sequential write bandwidth, in GB/s, over [base, base+bytes). */
static double write_bandwidth(void *base, size_t bytes)
{
  volatile uint64_t *p = base;
  size_t n = bytes / sizeof *p;
  double t0 = now_ns();
  for (size_t i = 0; i < n; i++) p[i] = i;
  double t1 = now_ns();
  return (double)bytes / (t1 - t0);   /* bytes/ns == GB/s */
}

/* Sequential read bandwidth, in GB/s. Returns the sum through [sink] so the
 * loop is not optimised away. */
static double read_bandwidth(void *base, size_t bytes, uint64_t *sink)
{
  volatile uint64_t *p = base;
  size_t n = bytes / sizeof *p;
  uint64_t acc = 0;
  double t0 = now_ns();
  for (size_t i = 0; i < n; i++) acc += p[i];
  double t1 = now_ns();
  *sink = acc;
  return (double)bytes / (t1 - t0);
}

/* Random dependent-load latency, in ns per access. The region is laid out as a
 * single random cycle over cache-line slots (Sattolo's algorithm); each slot
 * holds the byte offset of the next, so the chase is a chain of dependent loads
 * with no prefetchable pattern -- the classic idle-latency probe. The region
 * must exceed the last-level cache for this to measure memory, not cache. */
static double chase_latency(void *base, size_t bytes, uint64_t *sink)
{
  char *region = base;
  size_t n = bytes / CACHELINE;
  if (n < 2) return 0.0;

  size_t *order = malloc(n * sizeof *order);
  if (order == NULL) { perror("malloc"); exit(1); }
  for (size_t i = 0; i < n; i++) order[i] = i;
  for (size_t i = n - 1; i > 0; i--) {       /* Sattolo: one full cycle */
    size_t j = (size_t)(random() % (long)i);
    size_t tmp = order[i]; order[i] = order[j]; order[j] = tmp;
  }
  /* order[] is a permutation; thread it into a cycle slot -> order[slot]. */
  for (size_t i = 0; i < n; i++)
    *(size_t *)(region + i * CACHELINE) = order[i] * CACHELINE;
  free(order);

  size_t steps = n * 4;                       /* several laps to amortise */
  size_t off = 0;
  double t0 = now_ns();
  for (size_t k = 0; k < steps; k++)
    off = *(size_t *)(region + off);
  double t1 = now_ns();
  *sink = off;
  return (t1 - t0) / (double)steps;
}

static void bench_tier(const char *tier, void *base, size_t bytes)
{
  uint64_t sink = 0;
  memset(base, 0, bytes);                      /* fault the pages in */
  double wbw = write_bandwidth(base, bytes);
  double rbw = read_bandwidth(base, bytes, &sink);
  double lat = chase_latency(base, bytes, &sink);
  printf("%s,read_bw,%.3f,GB/s\n", tier, rbw);
  printf("%s,write_bw,%.3f,GB/s\n", tier, wbw);
  printf("%s,latency,%.2f,ns\n", tier, lat);
  fprintf(stderr, "[%s] read %.2f GB/s  write %.2f GB/s  latency %.1f ns "
                  "(ignore %llu)\n",
          tier, rbw, wbw, lat, (unsigned long long)sink);
}

int main(int argc, char **argv)
{
  const char *device = argc > 1 ? argv[1] : "/dev/dax1.0";
  size_t mib = argc > 2 ? (size_t)strtoull(argv[2], NULL, 0) : 512;
  size_t bytes = align_up(mib * 1024 * 1024, DAX_ALIGN);

  printf("tier,metric,value,unit\n");

  /* DRAM: a plain anonymous mapping. */
  void *dram = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (dram == MAP_FAILED) { perror("mmap dram"); return 1; }
  bench_tier("dram", dram, bytes);
  munmap(dram, bytes);

  /* Far: the devdax device, mapped MAP_SHARED as the runtime does. */
  int fd = open(device, O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "cannot open far device '%s'; skipping far tier\n", device);
    return 0;
  }
  void *far = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (far == MAP_FAILED) { perror("mmap far"); return 1; }
  bench_tier("far", far, bytes);
  munmap(far, bytes);
  return 0;
}
