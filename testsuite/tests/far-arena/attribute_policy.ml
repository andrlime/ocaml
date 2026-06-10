(* TEST
 set CAML_GC_POLICY = "attribute";
 modules = "stubs.c";
 native;
*)

(* The [attribute] policy routes objects by their compiler placement hint:
   [@far_memory] records go to the far arena on promotion, while unannotated
   objects follow the fallback sub-policy (all_dram by default) and stay in
   DRAM. Hints only reach the header on the native path. When far memory is
   unavailable the far requests fall back to DRAM, so nothing lands far. *)

external far_available : unit -> bool = "far_arena_available"
external arena_used : int -> int = "far_arena_used"

let far = 1

type t = { a : int; b : int }

(* Runtime field values, so neither block is lifted to a constant. *)
let[@inline never] mk_far x = ({ a = x; b = x } [@far_memory])
let[@inline never] mk_plain x = (x, x)

let () =
  let available = far_available () in
  let far_before = arena_used far in

  (* Far-annotated records: promoted to the far arena under [attribute]. *)
  let annotated = Array.init 20_000 (fun i -> mk_far i) in
  Gc.minor ();
  ignore (Sys.opaque_identity annotated);
  let far_after_annotated = arena_used far in

  (* Unannotated tuples: the fallback keeps them in DRAM, far is untouched. *)
  let plain = Array.init 20_000 (fun i -> mk_plain i) in
  Gc.minor ();
  ignore (Sys.opaque_identity plain);
  let far_after_plain = arena_used far in

  if available then
    assert (far_after_annotated > far_before)
  else
    assert (far_after_annotated = far_before);
  assert (far_after_plain = far_after_annotated);
  print_string "ok\n"
