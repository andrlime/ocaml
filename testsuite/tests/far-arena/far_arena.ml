(* TEST
 modules = "stubs.c";
*)

(* Objects requested into the far arena land on the far device when it is
   available and fall back to DRAM otherwise, but the allocation never fails.
   Per-arena accounting tracks the committed backing of each tier. *)

external far_available : unit -> bool = "far_arena_available"
external far_place : int -> int = "far_arena_place"
external arena_used : int -> int = "far_arena_used"

let dram = 0
let far = 1

let () =
  let available = far_available () in
  let expected = if available then 1 (* device *) else 2 (* DRAM *) in

  (* Pooled and large allocations land where expected and never fail. *)
  List.iter
    (fun words ->
       let where = far_place words in
       assert (where <> 0);
       assert (where = expected))
    [1; 4; 100; 1000; 100_000];

  (* A large object commits its own backing exactly, with no pool rounding, so
     the committed-words count of the arena it lands in grows by the object's
     size (plus a small fixed header). *)
  let placed = if available then far else dram in
  let words = 5000 in
  let before = arena_used placed in
  assert (far_place words = expected);
  let delta = arena_used placed - before in
  assert (delta >= words && delta <= words + 8);

  (* Cycle the far pools through a few major collections. *)
  for _ = 1 to 5 do Gc.full_major () done;
  print_string "ok\n"
