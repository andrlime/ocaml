(* TEST
 modules = "stubs.c";
 native;
*)

(* The [@far_memory] / [@main_memory] attributes lower to the reserved header
   bits of a young allocation, where the placement engine reads them
   (CAML_HINT_FAR = 2, CAML_HINT_MAIN = 1, CAML_HINT_NONE = 0). This is a
   native-only path: the bytecode allocator does not carry the hint.

   The fields are runtime values (via [Sys.opaque_identity]) so the blocks are
   genuinely allocated rather than lifted to constant static data. *)

external block_reserved : 'a -> int = "block_reserved"

type t = { a : int; b : int }

let[@inline never] mk_far x = ({ a = x; b = x } [@far_memory])
let[@inline never] mk_main x = ((x, x) [@main_memory])
let[@inline never] mk_plain x = (x, x)

let () =
  let x = Sys.opaque_identity 7 in
  assert (block_reserved (mk_far x) = 2);
  assert (block_reserved (mk_main x) = 1);
  assert (block_reserved (mk_plain x) = 0);
  print_string "ok\n"
