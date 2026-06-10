(* TEST
 set CAML_GC_POLICY = "flat_far";
 modules = "stubs.c";
*)

(* A large flat object is allocated directly into the major heap, bypassing the
   minor heap and promotion. Under flat_far it is placed in far memory, or falls
   back to DRAM when far memory is unavailable. *)

external far_available : unit -> bool = "far_arena_available"
external arena_used : int -> int = "far_arena_used"

let far = 1

let () =
  let available = far_available () in
  let before = arena_used far in
  (* ~25000 words: well above the minor-heap threshold, so this is a direct
     major-heap allocation, and flat, so flat_far sends it far. *)
  let big = Bytes.make 200_000 'x' in
  ignore (Sys.opaque_identity big);
  let after = arena_used far in
  if available then
    assert (after - before >= 25_000)
  else
    assert (after = before);
  print_string "ok\n"
