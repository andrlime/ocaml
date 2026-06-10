(* TEST
 set CAML_GC_POLICY = "all_dram";
 modules = "stubs.c";
*)

(* all_dram is the safe baseline: it behaves like stock OCaml and never routes
   any object to far memory. Allocate a mix of flat (bytes) and pointer-bearing
   (tuple) objects, promote them with a major collection, and confirm the far
   arena is never touched. This needs no far device, so it runs anywhere. *)

external arena_used : int -> int = "far_arena_used"

let far = 1

let () =
  let before = arena_used far in
  let flat = Array.init 20_000 (fun i -> Bytes.make 32 (Char.chr (i land 255))) in
  let ptrs = Array.init 20_000 (fun i -> (i, i)) in
  Gc.full_major ();
  ignore (Sys.opaque_identity flat);
  ignore (Sys.opaque_identity ptrs);
  assert (arena_used far = before);
  print_string "ok\n"
