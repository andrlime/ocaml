(* TEST
 modules = "stubs.c";
*)

(* Placement hints live in the reserved header bits and are read back at runtime
   via Reserved_hd. Check that every CAML_HINT_* value round-trips through a
   header unchanged. Pure header encoding -- no far device needed. *)

external hint_roundtrip : int -> int = "hint_roundtrip"

(* Mirrors the CAML_HINT_* constants in runtime/caml/placement.h. *)
let hint_none = 0
let hint_main = 1
let hint_far = 2

let () =
  List.iter
    (fun h -> assert (hint_roundtrip h = h))
    [hint_none; hint_main; hint_far];
  print_string "ok\n"
