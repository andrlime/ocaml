/**************************************************************************/
/*                                                                        */
/*                                 OCaml                                  */
/*                                                                        */
/*                          Tiered major heap                             */
/*                                                                        */
/**************************************************************************/

/* DAX-backed pool allocator. See runtime/caml/dax_arena.h for the contract.
 *
 * The arena is a single global object initialised lazily. After init succeeds
 * it owns a contiguous region of the device, split into two zones:
 *
 *   [base, large_zone_end)   - bump-pointer region for >SIZECLASS_MAX
 *                              allocations. Cursor advances under the lock.
 *   [large_zone_end, end)    - pool zone, sliced into Bsize_wsize(POOL_WSIZE)
 *                              chunks. Acquire pops from a LIFO free list,
 *                              then advances a cursor when the list is empty.
 *
 * Bump regions are not reclaimed; pool regions are reused indefinitely.
 *
 * Multi-arena bring-up (Week 4 of the tiered major-heap project): only one
 * device is supported. Extending to multiple far-memory arenas would require
 * one descriptor per device.
 */

#define CAML_INTERNALS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
/* DAX is a Linux feature; on Windows the arena is permanently unavailable. */
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif

#include "caml/config.h"
#include "caml/dax_arena.h"
#include "caml/misc.h"
#include "caml/platform.h"
#include "caml/sizeclasses.h"

#define DAX_DEFAULT_DEVICE   "/dev/dax2.0"
#define DAX_DEFAULT_BYTES    (8ULL * 1024 * 1024 * 1024)  /* 8 GiB */
#define DAX_LARGE_ZONE_BYTES (2ULL * 1024 * 1024 * 1024)  /* 2 GiB */
#define DAX_DEVICE_ALIGN     (2ULL * 1024 * 1024)         /* 2 MiB */
#define DAX_CACHELINE_BYTES  64

struct dax_pool_node {
  struct dax_pool_node* next;
};

struct dax_arena {
  /* Set once during init under [init_lock]; readable lock-free thereafter. */
  void*    base;
  size_t   total_bytes;
  size_t   large_zone_bytes;
  size_t   pool_bytes;          /* Bsize_wsize(POOL_WSIZE) */
  int      available;
  int      tried;
  int      fd;

  /* Mutates under [alloc_lock]. */
  caml_plat_mutex alloc_lock;
  size_t   large_cursor;        /* offset into [base, base+large_zone_bytes) */
  size_t   pool_cursor;         /* offset into [base+large_zone_bytes, base+total_bytes) */
  struct dax_pool_node* free_list;
  int      large_exhausted_warned;
  int      pool_exhausted_warned;
};

static struct dax_arena arena = {
  /* base */              NULL,
  /* total_bytes */       0,
  /* large_zone_bytes */  0,
  /* pool_bytes */        0,
  /* available */         0,
  /* tried */             0,
  /* fd */                -1,
  /* alloc_lock */        CAML_PLAT_MUTEX_INITIALIZER,
  /* large_cursor */      0,
  /* pool_cursor */       0,
  /* free_list */         NULL,
  /* large_exhausted_warned */ 0,
  /* pool_exhausted_warned */  0,
};

static caml_plat_mutex init_lock = CAML_PLAT_MUTEX_INITIALIZER;

static size_t round_up(size_t value, size_t align) {
  size_t mask = align - 1;
  return (value + mask) & ~mask;
}

/* Parse an unsigned 64-bit byte count with optional k/M/G suffix. Returns 0
 * on parse failure (which the caller treats as "use the default"). */
static unsigned long long parse_size(const char* s) {
  if (s == NULL || *s == '\0') return 0;
  char* endp = NULL;
  unsigned long long n = strtoull(s, &endp, 10);
  if (endp == s) return 0;
  if (endp != NULL && *endp != '\0') {
    char m = *endp;
    if (m == 'k' || m == 'K') n *= 1024ULL;
    else if (m == 'm' || m == 'M') n *= 1024ULL * 1024ULL;
    else if (m == 'g' || m == 'G') n *= 1024ULL * 1024ULL * 1024ULL;
    else return 0;
  }
  return n;
}

#ifdef _WIN32

void caml_dax_arena_init(void) {
  caml_plat_lock_blocking(&init_lock);
  arena.tried = 1;
  caml_plat_unlock(&init_lock);
}

#else

void caml_dax_arena_init(void) {
  caml_plat_lock_blocking(&init_lock);
  if (arena.tried) {
    caml_plat_unlock(&init_lock);
    return;
  }
  arena.tried = 1;

  const char* device = getenv("OCAML_DAX_DEVICE");
  if (device == NULL || *device == '\0') device = DAX_DEFAULT_DEVICE;

  unsigned long long requested = parse_size(getenv("OCAML_DAX_BYTES"));
  if (requested == 0) requested = DAX_DEFAULT_BYTES;
  size_t total = round_up((size_t)requested, DAX_DEVICE_ALIGN);

  int fd = open(device, O_RDWR);
  if (fd < 0) {
    fprintf(stderr,
            "[ocaml dax] open(%s) failed: %s; far arena disabled\n",
            device, strerror(errno));
    caml_plat_unlock(&init_lock);
    return;
  }

  void* base = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (base == MAP_FAILED) {
    fprintf(stderr,
            "[ocaml dax] mmap(%s, %zu bytes) failed: %s; far arena disabled\n",
            device, total, strerror(errno));
    close(fd);
    caml_plat_unlock(&init_lock);
    return;
  }

  size_t large = DAX_LARGE_ZONE_BYTES;
  if (large > total / 2) large = total / 2;
  large = round_up(large, DAX_DEVICE_ALIGN);

  size_t pool_bytes = (size_t)Bsize_wsize(POOL_WSIZE);
  /* Pools must tile 32 KiB aligned; the mmap base is 2 MiB aligned and
   * `large` is rounded to 2 MiB, so the pool zone start is 2 MiB aligned,
   * which is a multiple of pool_bytes. */
  CAMLassert(pool_bytes != 0);
  CAMLassert(large % pool_bytes == 0);

  arena.base = base;
  arena.total_bytes = total;
  arena.large_zone_bytes = large;
  arena.pool_bytes = pool_bytes;
  arena.fd = fd;
  arena.large_cursor = 0;
  arena.pool_cursor = 0;
  arena.free_list = NULL;
  arena.available = 1;

  caml_gc_log("DAX arena ready: device=%s base=%p total=%zu MiB "
              "large_zone=%zu MiB pool_zone=%zu MiB",
              device, base,
              total >> 20, large >> 20, (total - large) >> 20);

  caml_plat_unlock(&init_lock);
}

#endif

int caml_dax_arena_available(void) {
  /* `available` is written once, before any reader can race; tearing-free
   * read on all our supported architectures. */
  return arena.available;
}

void* caml_dax_pool_acquire(void) {
  if (!arena.available) return NULL;

  caml_plat_lock_blocking(&arena.alloc_lock);

  void* result = NULL;
  if (arena.free_list != NULL) {
    struct dax_pool_node* head = arena.free_list;
    arena.free_list = head->next;
    result = (void*)head;
  } else {
    size_t pool_zone_bytes = arena.total_bytes - arena.large_zone_bytes;
    if (arena.pool_cursor + arena.pool_bytes <= pool_zone_bytes) {
      result = (char*)arena.base + arena.large_zone_bytes + arena.pool_cursor;
      arena.pool_cursor += arena.pool_bytes;
    } else if (!arena.pool_exhausted_warned) {
      arena.pool_exhausted_warned = 1;
      fprintf(stderr,
              "[ocaml dax] pool zone exhausted (%zu bytes); "
              "subsequent far-arena pool requests will fail\n",
              pool_zone_bytes);
    }
  }

  caml_plat_unlock(&arena.alloc_lock);
  return result;
}

void caml_dax_pool_release(void* pool) {
  if (pool == NULL) return;
  struct dax_pool_node* node = (struct dax_pool_node*)pool;

  caml_plat_lock_blocking(&arena.alloc_lock);
  node->next = arena.free_list;
  arena.free_list = node;
  caml_plat_unlock(&arena.alloc_lock);
}

void* caml_dax_large_alloc(size_t bytes) {
  if (!arena.available || bytes == 0) return NULL;

  size_t aligned = round_up(bytes, DAX_CACHELINE_BYTES);

  caml_plat_lock_blocking(&arena.alloc_lock);

  void* result = NULL;
  if (aligned <= arena.large_zone_bytes &&
      arena.large_cursor + aligned <= arena.large_zone_bytes) {
    result = (char*)arena.base + arena.large_cursor;
    arena.large_cursor += aligned;
  } else if (!arena.large_exhausted_warned) {
    arena.large_exhausted_warned = 1;
    fprintf(stderr,
            "[ocaml dax] large zone exhausted (%zu bytes used of %zu); "
            "subsequent far-arena large allocations will fall back\n",
            arena.large_cursor, arena.large_zone_bytes);
  }

  caml_plat_unlock(&arena.alloc_lock);
  return result;
}

int caml_dax_contains(const void* p) {
  if (!arena.available) return 0;
  return (const char*)p >= (const char*)arena.base &&
         (const char*)p <  (const char*)arena.base + arena.total_bytes;
}

void caml_dax_arena_extent(void** base_out, size_t* bytes_out) {
  *base_out  = arena.available ? arena.base       : NULL;
  *bytes_out = arena.available ? arena.total_bytes : 0;
}
