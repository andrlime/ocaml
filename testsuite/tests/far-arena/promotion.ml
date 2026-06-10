(* TEST
 set CAML_GC_POLICY = "flat_far";
 modules = "stubs.c";
*)

(* Under the flat_far policy, promoting flat (No_scan) objects routes them to
   the far arena while pointer-bearing objects stay in DRAM. When far memory is
   unavailable the flat objects fall back to DRAM, so nothing lands far. *)

external far_available : unit -> bool = "far_arena_available"
external arena_used : int -> int = "far_arena_used"

let far = 1

let () =
  let available = far_available () in
  let before = arena_used far in
  (* Many small flat objects (bytes), kept live by an array, then promoted. *)
  let flat i = Bytes.make 8 (Char.chr (i land 255)) in
  let live = Array.init 20_000 flat in
  Gc.minor ();
  ignore (Sys.opaque_identity live);
  let after = arena_used far in
  if available then
    (* flat objects were promoted to far memory *)
    assert (after > before)
  else
    (* far unavailable: everything fell back to DRAM *)
    assert (after = before);
  print_string "ok\n"
