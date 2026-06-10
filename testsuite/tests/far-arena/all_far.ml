(* TEST
 set CAML_GC_POLICY = "all_far";
 modules = "stubs.c";
*)

(* all_far sends every object to far memory, including pointer-bearing ones,
   which flat_far would keep in DRAM. *)

external far_available : unit -> bool = "far_arena_available"
external arena_used : int -> int = "far_arena_used"

let far = 1

let () =
  let available = far_available () in
  let before = arena_used far in
  (* Pointer-bearing tuples, promoted from the minor heap. *)
  let live = Array.init 20_000 (fun i -> (i, i)) in
  Gc.minor ();
  ignore (Sys.opaque_identity live);
  let after = arena_used far in
  if available then
    assert (after > before)
  else
    assert (after = before);
  print_string "ok\n"
