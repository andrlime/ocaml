(* TEST
 set CAML_GC_POLICY = "attribute:size_threshold:64";
 modules = "stubs.c";
 native;
*)

(* The [attribute] policy honours the compiler hint and, for unannotated
   objects, defers to a fallback sub-policy named (with its own config) after a
   colon. Here the fallback is size_threshold with a 64-word threshold. So:
     - [@far_memory] records go far (by hint);
     - small unannotated tuples stay in DRAM (below the threshold);
     - large unannotated arrays go far (at or above the threshold).
   Hints reach the header only on the native path. With far memory unavailable
   the far requests fall back to DRAM, so nothing lands far.

   Objects are held in a list: cons cells are below the threshold, so the
   container itself stays in DRAM and does not perturb the far accounting. *)

external far_available : unit -> bool = "far_arena_available"
external arena_used : int -> int = "far_arena_used"

let far = 1

type t = { a : int; b : int }

let[@inline never] mk_far x = ({ a = x; b = x } [@far_memory])
let[@inline never] mk_small x = (x, x)             (* 2 words: below threshold *)
let[@inline never] mk_large x = Array.make 128 x   (* 128 words: at threshold *)

(* Build [n] objects kept live in a list, promoting them with a minor GC. *)
let live n f =
  let acc = ref [] in
  for i = 1 to n do acc := f i :: !acc done;
  Gc.minor ();
  Sys.opaque_identity acc

let () =
  let available = far_available () in

  let b0 = arena_used far in
  let _annotated = live 20_000 mk_far in     (* hint -> far *)
  let b1 = arena_used far in
  let _small = live 20_000 mk_small in        (* small fallback -> DRAM *)
  let b2 = arena_used far in
  let _large = live 2_000 mk_large in         (* large fallback -> far *)
  let b3 = arena_used far in

  if available then begin
    assert (b1 > b0);
    assert (b2 = b1);
    assert (b3 > b2)
  end else
    assert (b1 = b0 && b2 = b0 && b3 = b0);
  print_string "ok\n"
