#define CAML_INTERNALS

#include <caml/mlvalues.h>
#include <caml/domain_state.h>
#include <caml/shared_heap.h>
#include <caml/placement.h>
#include <caml/far_arena.h>

CAMLprim value far_arena_available(value unit)
{
  (void) unit;
  return Val_bool(caml_far_arena_available());
}

/* Allocate a [words]-word block into the far arena and report where it landed:
   0 = allocation failed, 1 = on the far device, 2 = fell back to DRAM. */
CAMLprim value far_arena_place(value words)
{
  mlsize_t wosize = Long_val(words);
  value *p = caml_shared_try_alloc_arena(Caml_state->shared_heap,
                                         wosize, 0, 0, CAML_ARENA_FAR);
  if (p == NULL) return Val_int(0);
  for (mlsize_t i = 0; i < wosize; i++)
    Field(Val_hp(p), i) = Val_unit;
  return Val_int(caml_far_arena_contains(p) ? 1 : 2);
}

/* Words of backing currently committed to the given arena. */
CAMLprim value far_arena_used(value arena)
{
  uintnat used[CAML_ARENA_COUNT];
  caml_shared_arena_used(used);
  return Val_long(used[Long_val(arena)]);
}
