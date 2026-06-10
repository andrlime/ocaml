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

#include "caml/far_arena.h"
#include "caml/misc.h"

#ifdef _WIN32

/* devdax is Linux-only; the far arena is simply unavailable elsewhere. */
int caml_far_arena_available(void) { return 0; }
void *caml_far_arena_alloc(uintnat bytes, uintnat align)
{
  (void) bytes; (void) align;
  return NULL;
}
int caml_far_arena_contains(const void *p) { (void) p; return 0; }
uintnat caml_far_arena_capacity(void) { return 0; }

#else

#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "caml/osdeps.h"
#include "caml/platform.h"

#define FAR_DEFAULT_DEVICE "/dev/dax1.0"
#define FAR_DEFAULT_BYTES  ((uintnat)16 << 30)   /* 16 GiB */
#define FAR_MAP_ALIGN      ((uintnat)2 << 20)    /* devdax 2 MiB alignment */

enum far_status { FAR_UNINIT, FAR_READY, FAR_UNAVAILABLE };

static struct {
  caml_plat_mutex lock;
  enum far_status status;
  char *base;
  uintnat capacity;   /* bytes mapped */
  uintnat offset;     /* bytes handed out so far */
} far = {
  CAML_PLAT_MUTEX_INITIALIZER,
  FAR_UNINIT,
  NULL,
  0,
  0
};

Caml_inline uintnat align_up(uintnat x, uintnat align)
{
  return (x + align - 1) & ~(align - 1);
}

/* Open and map the configured devdax device. Caller holds [far.lock] and
   [far.status] is FAR_UNINIT. */
static void map_device(void)
{
  const char *device = caml_secure_getenv(T("CAML_FAR_DEVICE"));
  const char *bytes_str = caml_secure_getenv(T("CAML_FAR_BYTES"));
  uintnat bytes;
  int fd;
  void *base;

  if (device == NULL) device = FAR_DEFAULT_DEVICE;
  bytes = bytes_str != NULL ? (uintnat) strtoull(bytes_str, NULL, 0) : 0;
  if (bytes == 0) bytes = FAR_DEFAULT_BYTES;
  bytes = align_up(bytes, FAR_MAP_ALIGN);

  fd = open(device, O_RDWR);
  if (fd < 0) {
    far.status = FAR_UNAVAILABLE;
    caml_gc_log("far arena: cannot open '%s'; far memory disabled", device);
    return;
  }

  base = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (base == MAP_FAILED) {
    far.status = FAR_UNAVAILABLE;
    caml_gc_log("far arena: cannot map '%s'; far memory disabled", device);
    return;
  }

  far.base = base;
  far.capacity = bytes;
  far.offset = 0;
  far.status = FAR_READY;
  caml_gc_log("far arena: mapped %" CAML_PRIuNAT " bytes of '%s' at %p",
              bytes, device, base);
}

static void ensure_initialised(void)
{
  if (far.status != FAR_UNINIT) return;
  caml_plat_lock_blocking(&far.lock);
  if (far.status == FAR_UNINIT) map_device();
  caml_plat_unlock(&far.lock);
}

int caml_far_arena_available(void)
{
  ensure_initialised();
  return far.status == FAR_READY;
}

void *caml_far_arena_alloc(uintnat bytes, uintnat align)
{
  void *p = NULL;

  ensure_initialised();
  if (far.status != FAR_READY) return NULL;

  caml_plat_lock_blocking(&far.lock);
  {
    uintnat start = align_up(far.offset, align);
    if (start + bytes <= far.capacity) {
      p = far.base + start;
      far.offset = start + bytes;
    }
  }
  caml_plat_unlock(&far.lock);

  if (p == NULL)
    caml_gc_log("far arena: exhausted (%" CAML_PRIuNAT " bytes mapped)",
                far.capacity);
  return p;
}

int caml_far_arena_contains(const void *p)
{
  return far.status == FAR_READY
      && (const char *)p >= far.base
      && (const char *)p < far.base + far.capacity;
}

uintnat caml_far_arena_capacity(void)
{
  ensure_initialised();
  return far.status == FAR_READY ? far.capacity : 0;
}

#endif /* _WIN32 */
