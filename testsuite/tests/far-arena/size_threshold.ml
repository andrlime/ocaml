(* TEST
 set CAML_GC_POLICY = "size_threshold:1000";
 modules = "stubs.c";
*)

(* size_threshold sends objects of at least its threshold (here 1000 words) to
   far memory and keeps smaller ones in DRAM, by size rather than by shape: a
   large pointer-bearing array goes far even though flat_far would keep it in
   DRAM. *)

external far_available : unit -> bool = "far_arena_available"
external arena_used : int -> int = "far_arena_used"

let far = 1

let () =
  let available = far_available () in
  let before = arena_used far in
  let big = Array.make 20_000 0 in   (* ~20000 words, well above threshold *)
  ignore (Sys.opaque_identity big);
  let after = arena_used far in
  if available then
    assert (after - before >= 20_000)
  else
    assert (after = before);
  print_string "ok\n"
