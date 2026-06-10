(* TEST
 set CAML_GC_POLICY = "flat_far";
 modules = "stubs.c";
*)

(* flat_far steers by the scannable bit: flat (No_scan) objects go to far
   memory, pointer-bearing objects stay in DRAM. Exercise both directions in
   one run -- a batch of flat bytes must grow the far arena, and a following
   batch of tuples must leave it untouched. When far memory is unavailable the
   flat objects fall back to DRAM, so nothing lands far. *)

external far_available : unit -> bool = "far_arena_available"
external arena_used : int -> int = "far_arena_used"

let far = 1

let () =
  let available = far_available () in

  (* Flat objects: routed to far on promotion. *)
  let far_before = arena_used far in
  let flat = Array.init 40_000 (fun i -> Bytes.make 16 (Char.chr (i land 255))) in
  Gc.minor ();
  ignore (Sys.opaque_identity flat);
  let far_after_flat = arena_used far in

  (* Pointer-bearing objects: kept in DRAM, so far must not grow further. *)
  let ptrs = Array.init 40_000 (fun i -> (i, i)) in
  Gc.minor ();
  ignore (Sys.opaque_identity ptrs);
  let far_after_ptrs = arena_used far in

  if available then
    assert (far_after_flat > far_before)
  else
    assert (far_after_flat = far_before);
  assert (far_after_ptrs = far_after_flat);
  print_string "ok\n"
