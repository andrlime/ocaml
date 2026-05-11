/* C stubs exposing DAX arena internals to the OCaml test. */

#define CAML_INTERNALS

#include "caml/mlvalues.h"
#include "caml/memory.h"
#include "caml/dax_arena.h"

/* Returns true if the DAX arena initialised successfully. */
CAMLprim value caml_test_dax_available(value unit)
{
  (void)unit;
  return Val_bool(caml_dax_arena_available());
}

/* Returns true if the block backing [v] is inside the DAX-mapped region.
 *
 * [v] must be a heap-allocated block (Is_block(v) == 1).  The OCaml value
 * points to the first field (one word past the header), so we probe the
 * header address (v - 1 word) to be safe, though both should be in range. */
CAMLprim value caml_test_dax_contains(value v)
{
  CAMLparam1(v);
  void* hdr = (void*)Hp_val(v);   /* header = one word before the value */
  CAMLreturn(Val_bool(caml_dax_contains(hdr)));
}

/* Returns (base_addr, total_bytes) as a pair of nativeints.
 * Both are 0n if the arena is unavailable. */
CAMLprim value caml_test_dax_extent(value unit)
{
  CAMLparam1(unit);
  CAMLlocal1(pair);
  void*  base  = NULL;
  size_t bytes = 0;
  caml_dax_arena_extent(&base, &bytes);
  pair = caml_alloc_tuple(2);
  Store_field(pair, 0, caml_copy_nativeint((intnat)base));
  Store_field(pair, 1, caml_copy_nativeint((intnat)bytes));
  CAMLreturn(pair);
}
